/*
 * See LICENSE for licensing information
 */

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/epoll.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <time.h>

#define COVER_PORT 12346

/* if the hostname option is specified, run as client, else run as server */
static const char* USAGE = "USAGE: cover [cover_server_hostname]\n";

/* all state where cover is stored here */
typedef struct _Cover Cover;
struct _Cover {
    /* the epoll descriptor to which we will add our sockets.
     * we use this descriptor with epoll to watch events on our sockets. */
    int ed;

    /* track if our client got a response and we can exit */
    int isDone;

    /* storage for client mode */
    struct {
        int sd;
        char* serverHostName;
	char* message;
        in_addr_t serverIP;
    } client;

    /* storage for server mode */
    struct {
        int sd;
    } server;
};

#define COVER_LOG_ERROR 1
#define COVER_LOG_WARNING 2
#define COVER_LOG_INFO 3
#define COVER_LOG_DEBUG 4

/* our cover code only relies on a log function, so let's supply that implementation here */
static void _cover_log(int level, const char* functionName, const char* format, ...) {
    char* levelString = "unknown";
    if(level == COVER_LOG_ERROR) {
        levelString = "error";
    } else if(level == COVER_LOG_WARNING) {
        levelString = "warning";
    } else if(level == COVER_LOG_INFO) {
        levelString = "info";
    } else if(level == COVER_LOG_DEBUG) {
        levelString = "debug";
    }

    printf("%li [%s] [%s] ", (long int) time(NULL), levelString, functionName);

    va_list variableArguments;
    va_start(variableArguments, format);
    vprintf(format, variableArguments);
    va_end(variableArguments);

    printf("%s", "\n");
}

static int _cover_startClient(Cover* h, char* serverHostname, char* message) {
    h->client.serverHostName = strndup(serverHostname, (size_t) 50);
    h->client.message = malloc(1 + (sizeof(char) * strlen(message)));
    strncpy(h->client.message, message, strlen(message));
    h->client.message[strlen(message)] = 0;
    // h->client.message = strndup(message, strlen(message));

    /* get the address of the server */
    struct addrinfo* serverInfo;
    int res = getaddrinfo(h->client.serverHostName, NULL, NULL, &serverInfo);
    if (res == -1) {
        _cover_log(COVER_LOG_ERROR, __FUNCTION__, "unable to start client: error in getaddrinfo");
        return -1;
    }

    h->client.serverIP = ((struct sockaddr_in*) (serverInfo->ai_addr))->sin_addr.s_addr;
    freeaddrinfo(serverInfo);

    /* create the client socket and get a socket descriptor */
    h->client.sd = socket(AF_INET, (SOCK_STREAM | SOCK_NONBLOCK), 0);
    if (h->client.sd == -1) {
        _cover_log(COVER_LOG_ERROR, __FUNCTION__, "unable to start client: error in socket");
        return -1;
    }

    /* our client socket address information for connecting to the server */
    struct sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = h->client.serverIP;
    serverAddress.sin_port = htons(COVER_PORT);

    /* connect to server. since we are non-blocking, we expect this to return EINPROGRESS */
    res = connect(h->client.sd, (struct sockaddr *) &serverAddress, sizeof(serverAddress));
    if (res == -1 && errno != EINPROGRESS) {
        _cover_log(COVER_LOG_ERROR, __FUNCTION__, "unable to start client: error in connect");
        return -1;
    }

    /* specify the events to watch for on this socket.
     * the client wants to know when it can send a cover message. */
    struct epoll_event ev;
    ev.events = EPOLLOUT;
    ev.data.fd = h->client.sd;

    /* start watching the client socket */
    res = epoll_ctl(h->ed, EPOLL_CTL_ADD, h->client.sd, &ev);
    if (res == -1) {
        _cover_log(COVER_LOG_ERROR, __FUNCTION__, "unable to start client: error in epoll_ctl");
        return -1;
    }

    /* success! */
    return 0;
}

static int _cover_startServer(Cover* h) {
    /* create the socket and get a socket descriptor */
    h->server.sd = socket(AF_INET, (SOCK_STREAM | SOCK_NONBLOCK), 0);
    if (h->server.sd == -1) {
        _cover_log(COVER_LOG_ERROR, __FUNCTION__, "unable to start server: error in socket");
        return -1;
    }

    /* setup the socket address info, client has outgoing connection to server */
    struct sockaddr_in bindAddress;
    memset(&bindAddress, 0, sizeof(bindAddress));
    bindAddress.sin_family = AF_INET;
    bindAddress.sin_addr.s_addr = INADDR_ANY;
    bindAddress.sin_port = htons(COVER_PORT);

    /* bind the socket to the server port */
    int res = bind(h->server.sd, (struct sockaddr *) &bindAddress, sizeof(bindAddress));
    if (res == -1) {
        _cover_log(COVER_LOG_ERROR, __FUNCTION__, "unable to start server: error in bind");
        return -1;
    }

    /* set as server socket that will listen for clients */
    res = listen(h->server.sd, 100);
    if (res == -1) {
        _cover_log(COVER_LOG_ERROR, __FUNCTION__, "unable to start server: error in listen");
        return -1;
    }

    /* specify the events to watch for on this socket.
     * the server wants to know when a client is connecting. */
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = h->server.sd;

    /* start watching the server socket */
    res = epoll_ctl(h->ed, EPOLL_CTL_ADD, h->server.sd, &ev);
    if (res == -1) {
        _cover_log(COVER_LOG_ERROR, __FUNCTION__, "unable to start server: error in epoll_ctl");
        return -1;
    }

    /* success! */
    return 0;
}

static void _cover_free(Cover* h) {
    assert(h);

    if (h->client.sd)
        close(h->client.sd);
    if (h->client.serverHostName)
        free(h->client.serverHostName);
    if (h->ed)
        close(h->ed);

    free(h);
}

static Cover* _cover_new(int argc, char* argv[]) {
    /*if (argc < 1 || argc > 4) {
        _cover_log(COVER_LOG_WARNING, __FUNCTION__, USAGE);
        return NULL;
    }*/
    /* use epoll to asynchronously watch events for all of our sockets */
    int mainEpollDescriptor = epoll_create(1);
    if (mainEpollDescriptor == -1) {
        _cover_log(COVER_LOG_ERROR, __FUNCTION__, "Error in main epoll_create");
        close(mainEpollDescriptor);
        return NULL;
    }

    /* get memory for the new state */
    Cover* h = calloc(1, sizeof(Cover));
    assert(h);

    h->ed = mainEpollDescriptor;
    h->isDone = 0;

    /* extract the server hostname from argv if in client mode */
    int isFail = 0;
    if (argc == 3) {
        /* client mode */
	_cover_log(COVER_LOG_INFO, __FUNCTION__, "Starting client: %s", argv[2]);
        isFail = _cover_startClient(h, argv[1], argv[2]);
    } else {
        /* server mode */
        isFail = _cover_startServer(h);
    }

    if (isFail) {
        _cover_free(h);
        return NULL;
    } else {
        return h;
    }
}

static void _cover_activateClient(Cover* h, int sd, uint32_t events) {
    ssize_t numBytes = 0;
    char message[1000];
    assert(h->client.sd == sd);

    if (events & EPOLLOUT) {
        _cover_log(COVER_LOG_INFO, __FUNCTION__, "EPOLLOUT is set");
    }
    if (events & EPOLLIN) {
        _cover_log(COVER_LOG_INFO, __FUNCTION__, "EPOLLIN is set");
    }

    /* to keep things simple, there is explicitly no resilience here.
     * we allow only one chance to send the message and one to receive the response.
     */

    if (events & EPOLLOUT) {
        /* the kernel can accept data from us,
         * and we care because we registered EPOLLOUT on sd with epoll */
	char response[1000];
	snprintf(response, 1000, "[%s] COVER REQUEST", h->client.message);
        /* prepare the message */
        memset(message, 0, (size_t) 1000);
        snprintf(message, strlen(response) + 1, "%s", response);

        int q = 0;
        for (q = 0; q < 200; q++) {
	// while (1) {
                /* send the message */
                numBytes = send(sd, message, strlen(response), 0);

                /* log result */
                if (numBytes > 0) {
                    _cover_log(COVER_LOG_INFO, __FUNCTION__, "successfully sent '%s' message", message);
                } else {
                    _cover_log(COVER_LOG_WARNING, __FUNCTION__, "unable to send message");
                }

        }
	/* tell epoll we don't care about writing anymore */
	struct epoll_event ev;
	memset(&ev, 0, sizeof(struct epoll_event));
	ev.events = EPOLLIN;
	ev.data.fd = sd;
	epoll_ctl(h->ed, EPOLL_CTL_MOD, sd, &ev);
    } else if (events & EPOLLIN) {
        /* there is data available to read from the kernel,
         * and we care because we registered EPOLLIN on sd with epoll */
        int q = 0;
        for (q = 0; q < 200; q++) {
                /* prepare to accept the message */
                memset(message, 0, (size_t) 4);

                numBytes = recv(sd, message, (size_t) 4, 0);

                /* log result */
                if (numBytes > 0) {
                    _cover_log(COVER_LOG_INFO, __FUNCTION__, "successfully received '%s' message",
                            message);
                } else {
                    _cover_log(COVER_LOG_WARNING, __FUNCTION__, "unable to receive message");
                }

        }
	/* tell epoll we no longer want to watch this socket */
	epoll_ctl(h->ed, EPOLL_CTL_DEL, sd, NULL);

	close(sd);
	h->client.sd = 0;
	h->isDone = 1;
    }
}

static void _cover_activateServer(Cover* h, int sd, uint32_t events) {
    ssize_t numBytes = 0;
    char message[1000];
    struct epoll_event ev;

    if (events & EPOLLOUT) {
        _cover_log(COVER_LOG_INFO, __FUNCTION__, "EPOLLOUT is set");
    }
    if (events & EPOLLIN) {
        _cover_log(COVER_LOG_INFO, __FUNCTION__, "EPOLLIN is set");
    }

    if (sd == h->server.sd) {
        /* data on a listening socket means a new client connection */
        assert(events & EPOLLIN);

        /* accept new connection from a remote client */
        int newClientSD = accept(sd, NULL, NULL);

        /* now register this new socket so we know when its ready */
        memset(&ev, 0, sizeof(struct epoll_event));
        ev.events = EPOLLIN;
        ev.data.fd = newClientSD;
        epoll_ctl(h->ed, EPOLL_CTL_ADD, newClientSD, &ev);
    } else {
        /* a client is communicating with us over an existing connection */
        if (events & EPOLLIN) {
            /* prepare to accept the message */
            memset(message, 0, (size_t) 1000);
            numBytes = recv(sd, message, (size_t) 1000, 0);

            /* log result */
            if (numBytes > 0) {
                _cover_log(COVER_LOG_INFO, __FUNCTION__, "successfully received '%s' message from: '%i'", message, sd);
            } else if (numBytes == 0) {
                /* client got response and closed */
                /* tell epoll we no longer want to watch this socket */
                epoll_ctl(h->ed, EPOLL_CTL_DEL, sd, NULL);
                close(sd);
            } else {
                _cover_log(COVER_LOG_WARNING, __FUNCTION__, "unable to receive message");
            }
		
            /* tell epoll we want to write the response now */
            memset(&ev, 0, sizeof(struct epoll_event));
            ev.events = EPOLLOUT;
            ev.data.fd = sd;
            epoll_ctl(h->ed, EPOLL_CTL_MOD, sd, &ev);
        } else if (events & EPOLLOUT) {
            /* prepare the response message */
	    char response[] = "ACK";
            memset(message, 0, (size_t) 4);
            snprintf(message, strlen(response) + 1, "%s", response);

            /* send the message */
            numBytes = send(sd, message, strlen(response) + 1, 0);

            /* log result */
            if (numBytes > 0) {
                _cover_log(COVER_LOG_INFO, __FUNCTION__, "successfully sent '%s' message",
                        message);
            } else {
                _cover_log(COVER_LOG_WARNING, __FUNCTION__, "unable to send message");
            }

            /* now wait until we read 0 for client close event */
            memset(&ev, 0, sizeof(struct epoll_event));
            ev.events = EPOLLIN;
            ev.data.fd = sd;
            epoll_ctl(h->ed, EPOLL_CTL_MOD, sd, &ev);
        }
    }
}

static void _cover_ready(Cover* h) {
    assert(h);

    /* collect the events that are ready */
    struct epoll_event epevs[10];
    int nfds = epoll_wait(h->ed, epevs, 10, 0);
    if (nfds == -1) {
        _cover_log(COVER_LOG_ERROR, __FUNCTION__, "error in epoll_wait");
        return;
    }

    /* activate correct component for every socket thats ready */
    for (int i = 0; i < nfds; i++) {
        int d = epevs[i].data.fd;
        uint32_t e = epevs[i].events;
        if (d == h->client.sd) {
            _cover_activateClient(h, d, e);
        } else {
            _cover_activateServer(h, d, e);
        }
    }
}

static int _cover_getEpollDescriptor(Cover* h) {
    assert(h);
    return h->ed;
}

static int _cover_isDone(Cover* h) {
    assert(h);
    return h->isDone;
}

/* program execution starts here */
int main(int argc, char *argv[]) {
    _cover_log(COVER_LOG_INFO, __FUNCTION__, "Starting cover function");

    /* create the new state according to user inputs */
    Cover* coverState = _cover_new(argc, argv);
    if (!coverState) {
        _cover_log(COVER_LOG_INFO, __FUNCTION__, "Error initializing new Cover instance");
        return -1;
    }

    /* now we need to watch all of the cover descriptors in our main loop
     * so we know when we can wait on any of them without blocking. */
    int mainepolld = epoll_create(1);
    if (mainepolld == -1) {
        _cover_log(COVER_LOG_INFO, __FUNCTION__, "Error in main epoll_create");
        close(mainepolld);
        return -1;
    }

    /* cover has one main epoll descriptor that watches all of its sockets,
     * so we now register that descriptor so we can watch for its events */
    struct epoll_event mainevent;
    mainevent.events = EPOLLIN | EPOLLOUT;
    mainevent.data.fd = _cover_getEpollDescriptor(coverState);
    if (!mainevent.data.fd) {
        _cover_log(COVER_LOG_INFO, __FUNCTION__, "Error retrieving cover epoll descriptor");
        close(mainepolld);
        return -1;
    }
    epoll_ctl(mainepolld, EPOLL_CTL_ADD, mainevent.data.fd, &mainevent);

    /* main loop - wait for events from the cover descriptors */
    struct epoll_event events[100];
    int nReadyFDs;
    _cover_log(COVER_LOG_INFO, __FUNCTION__, "entering main loop to watch descriptors");

    while (1) {
        /* wait for some events */
        _cover_log(COVER_LOG_INFO, __FUNCTION__, "waiting for events");
        nReadyFDs = epoll_wait(mainepolld, events, 100, -1);
        if (nReadyFDs == -1) {
            _cover_log(COVER_LOG_INFO, __FUNCTION__, "Error in client epoll_wait");
            return -1;
        }

        /* activate if something is ready */
        _cover_log(COVER_LOG_INFO, __FUNCTION__, "processing event");
        if (nReadyFDs > 0) {
            _cover_ready(coverState);
        }

        /* break out if cover is done */
        if (_cover_isDone(coverState)) {
            break;
        }
    }

    _cover_log(COVER_LOG_INFO, __FUNCTION__, "finished main loop, cleaning up");

    /* de-register the cover epoll descriptor */
    mainevent.data.fd = _cover_getEpollDescriptor(coverState);
    if (mainevent.data.fd) {
        epoll_ctl(mainepolld, EPOLL_CTL_DEL, mainevent.data.fd, &mainevent);
    }

    /* cleanup and close */
    close(mainepolld);
    _cover_free(coverState);

    _cover_log(COVER_LOG_INFO, __FUNCTION__, "exiting cleanly");

    return 0;
}
