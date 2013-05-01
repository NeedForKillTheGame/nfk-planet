#define FD_SETSIZE 256

#include <stdio.h>
#include <time.h>


#ifdef _WIN32

#include <windows.h>
#include <process.h>

#define IN_ADDR__S_ADDR     S_un.S_addr
#define CLOSESOCKET         closesocket
#define MSG_NOSIGNAL		0

#define SLEEP(x)            Sleep((x) * 1000)

#define PLANET_CODEPAGE     1251

/* global var for mutex */
HANDLE planetMutex;
#define THREAD_RETURN_TYPE  unsigned __stdcall
#define LOCK_INIT           (planetMutex = CreateMutex(NULL, 0, NULL))
#define LOCK                WaitForSingleObject(planetMutex, INFINITE)
#define UNLOCK              ReleaseMutex(planetMutex)

#else       /* not _WIN32 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <wchar.h>
#include <iconv.h>
#include <pthread.h>

#define INVALID_SOCKET      -1
#define SOCKET_ERROR        -1
#define SOCKET              int
#define IN_ADDR__S_ADDR     s_addr
#define CLOSESOCKET         close

#define SLEEP(x)            sleep(x)

#define PLANET_CODEPAGE     "CP1251"

#define    min(a,b) (((a) < (b)) ? (a) : (b))

/* global var for mutex */
pthread_mutex_t planetMutex;
#define THREAD_RETURN_TYPE  void *

#define LOCK_INIT           initMutex()
#define LOCK                pthread_mutex_lock(&planetMutex)
#define UNLOCK              pthread_mutex_unlock(&planetMutex)

#endif

#define PLANET_PORT         10003
#define PLANET_HOST         "127.0.0.1"     // change this to your server IP
#define PLANET_VERSION      "077"
#define PLANET_VERSION_NUMBER 77
#define PLANET_MAX_INPUT    256
#define PLANET_MAX_CLIENTS  256
/* ping timeout for clients in seconds */
#define CLIENT_PING_TIMEOUT 600

typedef struct _planet_client
{
    struct _planet_client *prev;
    struct _planet_client *next;
    SOCKET s;
    char ip[16];
    /* NFK server port, not the Planet client port */
    unsigned short port;
    /* reported (or predicted) NFK version of the client */
    unsigned int version;
    /* buffer for data received from client */
    char inBuf[PLANET_MAX_INPUT];
    unsigned int inBufLen;
    /* buffer for data to be sent to client */
    char *outBuf;
    unsigned int outBufLen;
    unsigned int outBufCapacity;
    /* indicates that there's data to be sent to client */
    int writing;
    /* server that the client has created */
    struct _planet_server *server;
	time_t lastPingTime;
} planet_client;

typedef struct _planet_server
{
    struct _planet_server *prev;
    struct _planet_server *next;
    struct _planet_client *owner;
    wchar_t hostname[32];
    wchar_t mapname[32];
    char gametype;
    char cur_users;
    char max_users;
} planet_server;

planet_client *firstClient = NULL;
planet_client *lastClient = NULL;
unsigned int numberOfClients = 0;

planet_server *firstServer = NULL;
planet_server *lastServer = NULL;
unsigned int numberOfServers = 0;

const char oldVersionMsg[] = "L127.0.0.1\rYour version of NF\rK is too old\r1\r1\r1\r\n\0"
                             "L127.0.0.1\rPlease download\rthe latest version\r1\r1\r1\r\n\0"
                             "L127.0.0.1\rfrom\r^2needforkill.ru     \r1\r1\r1\r\n\0"
							 "L127.0.0.1\r\r\r1\r1\r1\r\n\0"
							 "L127.0.0.1\rCKA4AUTE HOBY|-0\rNFK C CAUTA\r1\r1\r1\r\n\0"
							 "L127.0.0.1\r^2needforkill.ru    \r\r1\r1\r1\r\n\0E\n\0";

void planetServerRemove(planet_server *server);

#ifdef _WIN32

// Unicode to MultiByte convert, Win32 version
int convertToMb(char *dst, wchar_t *src, int maxLength)
{
    int srcLen = wcsnlen(src, maxLength);
    int retn;

    memset(dst, 0, maxLength);
    retn = WideCharToMultiByte(PLANET_CODEPAGE, 0, src, srcLen, dst, maxLength, NULL, NULL);
    if (retn == 0)
    {
        return strnlen(dst, maxLength);
    }
    return retn;
}

// MultiByte to Unicode convert, Win32 version
int convertToWc(wchar_t *dst, char *src, int maxLength)
{
    int srcLen = strnlen(src, maxLength);

    return MultiByteToWideChar(PLANET_CODEPAGE, 0, src, srcLen, dst, maxLength);
}

#else

/* don't remember exactly why did I put it here
#ifndef strnlen
int strnlen(const char *s, int maxLength)
{
    int len;
    
    for (len = 0; *s && maxLength > 0; s++, len++, maxLength--);
    
    return len;
}
#endif

#ifndef wcsnlen
int wcsnlen(const wchar_t *s, int maxLength)
{
    int len;
    
    for (len = 0; *s && maxLength > 0; s++, len++, maxLength--);
    
    return len;
}
#endif
*/

// Unicode to MultiByte convert, iconv version
int convertToMb(char *dst, wchar_t *src, int maxLength)
{
    wchar_t *srcPtr = src;
    char *dstPtr = dst;
    size_t srcLen;
    size_t dstLen = maxLength;
    iconv_t cd = iconv_open(PLANET_CODEPAGE, "WCHAR_T");

    if (cd == (iconv_t)-1)
    {
        return 0;
    }

    srcLen = wcsnlen(src, maxLength) * sizeof(wchar_t);

    iconv(cd, (const char**)&srcPtr, &srcLen, (char**)&dstPtr, &dstLen);

    iconv_close(cd);

    return dstPtr - dst;
}

// MultiByte to Unicode convert, iconv version
int convertToWc(wchar_t *dst, char *src, int maxLength)
{
    char *srcPtr = src;
    wchar_t *dstPtr = dst;
    size_t srcLen;
    size_t dstLen = maxLength * sizeof(wchar_t);
    iconv_t cd = iconv_open("WCHAR_T", PLANET_CODEPAGE);

    if (cd == (iconv_t)-1)
    {
        return 0;
    }

    srcLen = strnlen(src, maxLength);

    iconv(cd, (const char**)&srcPtr, &srcLen, (char**)&dstPtr, &dstLen);

    iconv_close(cd);

    return dstPtr - dst;
}

// mutexes on *nix is hard
int initMutex()
{
	pthread_mutexattr_t attr;

	if (pthread_mutexattr_init(&attr))
	{
		return 0;
	}

	if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE))
	{
		pthread_mutexattr_destroy(&attr);
		return 0;
	}

	if (pthread_mutex_init(&planetMutex, &attr))
	{
		pthread_mutexattr_destroy(&attr);
		return 0;
	}

	return 1;
}

#endif

/* OOP is for wimps */
/* we're not locking global mutex in these two procedures because we hope the caller did it */
void planetClientAdd(planet_client *client)
{
    numberOfClients++;

    client->next = NULL;

    if (firstClient == NULL)
    {
        firstClient = client;
        lastClient = client;
        client->prev = NULL;
        return;
    }

    lastClient->next = client;
    client->prev = lastClient;
    lastClient = client;
}

void planetClientRemove(planet_client *client)
{
    if (firstClient == client)
    {
        firstClient = client->next;
    }

    if (lastClient == client)
    {
        lastClient = client->prev;
    }

    if (client->prev != NULL)
    {
        client->prev->next = client->next;
    }

    if (client->next != NULL)
    {
        client->next->prev = client->prev;
    }
    numberOfClients--;

    if (client->outBuf != NULL)
        free(client->outBuf);
    CLOSESOCKET(client->s);
    if (client->server != NULL)
    {
        planetServerRemove(client->server);
    }
    free(client);
}

void planetServerAdd(planet_server *server)
{
    numberOfServers++;

    server->next = NULL;

    if (firstServer == NULL)
    {
        firstServer = server;
        lastServer = server;
        server->prev = NULL;
        return;
    }

    lastServer->next = server;
    server->prev = lastServer;
    lastServer = server;
}

void planetServerRemove(planet_server *server)
{
    if (firstServer == server)
    {
        firstServer = server->next;
    }

    if (lastServer == server)
    {
        lastServer = server->prev;
    }

    if (server->prev != NULL)
    {
        server->prev->next = server->next;
    }

    if (server->next != NULL)
    {
        server->next->prev = server->prev;
    }
    numberOfServers--;
}

void onClientConnect(SOCKET serv)
{
    struct sockaddr_in addr;
    unsigned int alen;
    SOCKET clientSock;
    planet_client *client;

    memset(&addr, 0, sizeof(addr));
    alen = sizeof(addr);
    clientSock = accept(serv, (struct sockaddr*)&addr, &alen);
    if (clientSock == INVALID_SOCKET)
    {
        wprintf(L"accept() failed\n");
        return;
    }

    LOCK;
    if (numberOfClients == PLANET_MAX_CLIENTS)
    {
        /* max clients reached, closing connection */
        CLOSESOCKET(clientSock);
        UNLOCK;
        return;
    }

    client = (planet_client *)malloc(sizeof(planet_client));
    if (client == NULL)
    {
        wprintf(L"Out of memory, client dropped\n");
        CLOSESOCKET(clientSock);
        UNLOCK;
        return;
    }
    client->s = clientSock;
    client->inBufLen = 0;
    strcpy(client->ip, inet_ntoa(addr.sin_addr));
    client->port = 0;
    client->version = 0;
    client->writing = 0;
    client->server = NULL;
	time(&client->lastPingTime);
    /* initial size of outBuffer - 2kb */
    client->outBufLen = 0;
    client->outBufCapacity = 2048;
    client->outBuf = (char *)malloc(2048);
    if (client->outBuf == NULL)
    {
        wprintf(L"Out of memory, client dropped\n");
        free(client);
        CLOSESOCKET(clientSock);
        UNLOCK;
        return;
    }
    planetClientAdd(client);

    UNLOCK;
}

/* onClientCommand is called each time \r\n is found in the input. 
    Responding to the command */
int onClientCommand(planet_client *client, char *command, int cmdLen)
{
    if (cmdLen < 2)
    {
        wprintf(L"Client %hs sent too short command, dropped\n", client->ip);
        return 0;
    }

    if (command[0] != '?')
    {
        wprintf(L"Client %hs sent invalid command first byte, dropped\n", client->ip);
        return 0;
    }

    /* client must ask for Planet version first (since 077 client also reports its version) */
    if (client->version == 0 && command[1] != 'V')
    {
        wprintf(L"Client %hs did not provide its version first, dropped\n", client->ip);
        return 0;
    }
    
    switch (command[1])
    {
    case 'V':   /* version request */
        if (cmdLen == 2)
        {
            /* report V075 to old clients */
            client->version = 75;
            strcpy(client->outBuf, "V075\n");
            client->outBufLen = 6;
        }
        else
        {
            /* extract and save client NFK version */
            client->version = atoi(command + 2);
            /* report current Planet version */
            sprintf(client->outBuf, "V%s\n", PLANET_VERSION);
            client->outBufLen = (unsigned int)strlen(PLANET_VERSION) + 3;
        }
        client->writing = 1;
        break;
    case 'G':   /* servers list request */
        {
            if (client->version < PLANET_VERSION_NUMBER)
            {
                /* send the message to old clients */
                memcpy(client->outBuf + client->outBufLen, oldVersionMsg, sizeof(oldVersionMsg) - 1);
                client->outBufLen += sizeof(oldVersionMsg) - 1;
                client->writing = 1;
            }
            else
            {
                planet_server *currentServer = firstServer;
                unsigned int maxInfoSize;

                if (client->outBufLen > 16)
                {
                    /* if theres something big in output buffer (most likely previous ?G command response),
                       silently ignore current request */
                    return 1;
                }
                
                /* want to know how magic 90 was computed? see below */
                maxInfoSize = 90 * numberOfServers + 3;
                /* check if current out buffer is capable of sending all servers info */
                if (client->outBufCapacity < maxInfoSize)
                {
                    /* grow */
                    client->outBufCapacity = (maxInfoSize + 1024) & 0x3F;
                    client->outBuf = realloc(client->outBuf, client->outBufCapacity);
                    if (client->outBuf == NULL)
                    {
                        wprintf(L"Out of memory, client dropped\n");
                        return 0;
                    }
                }


                while (currentServer != NULL)
/* max output */{
/* 1 */             client->outBuf[client->outBufLen++] = 'L';
/* 16 */            strcpy(client->outBuf + client->outBufLen, currentServer->owner->ip);
                    client->outBufLen += strlen(currentServer->owner->ip);
/* 17 */            client->outBuf[client->outBufLen++] = '\r';
/* 47 */            client->outBufLen += convertToMb(client->outBuf + client->outBufLen, currentServer->hostname, 30);
/* 48 */            client->outBuf[client->outBufLen++] = '\r';
/* 78 */            client->outBufLen += convertToMb(client->outBuf + client->outBufLen, currentServer->mapname, 30);
/* 79 */            client->outBuf[client->outBufLen++] = '\r';
/* 77 */            client->outBuf[client->outBufLen++] = currentServer->gametype;
/* 78 */            client->outBuf[client->outBufLen++] = '\r';
/* 79 */            client->outBuf[client->outBufLen++] = currentServer->cur_users;
/* 80 */            client->outBuf[client->outBufLen++] = '\r';
/* 81 */            client->outBuf[client->outBufLen++] = currentServer->max_users;
/* 82 */            client->outBuf[client->outBufLen++] = '\r';
                    if (client->version >= 76)
                    {
                        char portbuf[8];
                        int len;

                        /* _itoa(currentServer->owner->port, portbuf, 10); */
                        sprintf(portbuf, "%u", currentServer->owner->port);
                        len = strlen(portbuf);
/* 87 */                strcpy(client->outBuf + client->outBufLen, portbuf);
                        client->outBufLen += len;

/* 88 */                client->outBuf[client->outBufLen++] = '\r';
                    }
/* 89 */            client->outBuf[client->outBufLen++] = '\n';
/* 90 */            client->outBuf[client->outBufLen++] = '\0';

                    currentServer = currentServer->next;
                }
                strcpy(client->outBuf + client->outBufLen, "E\n");
                client->outBufLen += 3;
                client->writing = 1;
            }
        }
        break;
    case 'R':   /* register new server */
        {
            planet_server *newServer;

            if (client->server != NULL)
            {
                wprintf(L"Client %hs tried to register server twice, dropped\n", client->ip);
                return 0;
            }

            if ((client->outBufCapacity - client->outBufLen) < 3)
            {
                wprintf(L"Client %hs reached maximum output, dropped\n", client->ip);
                return 0;
            }

            /* don't let old clients create servers, drop them instead */
			if (client->version < 76)
			{
				return 0;
			}
			
			client->port = atoi(command + 2);
			
            for (newServer = firstServer; newServer; newServer = newServer->next)
            {
                if (strcmp(newServer->owner->ip, client->ip) == 0 && newServer->owner->port == client->port)
                {
                    /* delete duplicate servers (with same IP and Port) */
                    planetClientRemove(newServer->owner);
                    wprintf(L"Client %hs tried to create server twice. Duplicate removed\n", client->ip);
                    break;
                }
            }

            newServer = (planet_server*)malloc(sizeof(planet_server));

            if (newServer == NULL)
            {
                wprintf(L"Out of memory, client dropped\n");
                return 0;
            }
			
            client->server = newServer;
            newServer->owner = client;
            wcscpy(newServer->hostname, L"null");
            wcscpy(newServer->mapname, L"null");
            newServer->cur_users = '0';
            newServer->max_users = '8';
            newServer->gametype = '0';
            planetServerAdd(newServer);
            strcpy(client->outBuf + client->outBufLen, "r\n");
            client->outBufLen += 3;
            client->writing = 1;
        }
        break;
    case 'N':   /* set server name */
        {
            if (client->server == NULL)
            {
                wprintf(L"Client %hs has tried to set hostname not being a server\n", client->ip);
                return 0;
            }

            client->server->hostname[convertToWc(client->server->hostname, command + 2, 30)] = L'\0';
        }
        break;
    case 'm':   /* set server map */
        {
            if (client->server == NULL)
            {
                wprintf(L"Client %hs has tried to set mapname not being a server\n", client->ip);
                return 0;
            }

            client->server->mapname[convertToWc(client->server->mapname, command + 2, 30)] = L'\0';
        }
        break;
    case 'C':   /* set players count */
        {
            if (client->server == NULL)
            {
                wprintf(L"Client %hs has tried to set current users count not being a server\n", client->ip);
                return 0;
            }

            client->server->cur_users = command[2];
        }
        break;
    case 'M':   /* set max players count */
        {
            if (client->server == NULL)
            {
                wprintf(L"Client %hs has tried to set max users count not being a server\n", client->ip);
                return 0;
            }

            client->server->max_users = command[2];
        }
        break;
    case 'P':   /* set server game type */
        {
            if (client->server == NULL)
            {
                wprintf(L"Client %hs has tried to set gametype not being a server\n", client->ip);
                return 0;
            }

            client->server->gametype = command[2];
        }
        break;
    case 'S':   /* get number of clients */
        {
            if ((client->outBufCapacity - client->outBufLen) < 10)
            {
                wprintf(L"Client %hs reached maximum output, dropped\n", client->ip);
                return 0;
            }

            client->outBufLen += sprintf(client->outBuf + client->outBufLen, "S%u\n", numberOfClients) + 1;
            client->writing = 1;
        }
        break;
    case 'K':   /* ping */
        {
            if ((client->outBufCapacity - client->outBufLen) < 3)
            {
                wprintf(L"Client %hs reached maximum output, dropped\n", client->ip);
                return 0;
            }

            strcpy(client->outBuf + client->outBufLen, "K\n");
            client->outBufLen += 3;
            client->writing = 1;

			time(&client->lastPingTime);
        }
        break;
    case 'X':   /* ask for invite */
        {
            planet_server *currentServer = firstServer;
            char address[16];
            int port;
            char *temp;

            temp = strchr(command, ':');
            address[15] = '\0';
            if (temp == NULL)
            {
                strncpy(address, command + 2, 15);
                port = 0;
            }
            else
            {
                strncpy(address, command + 2, min(temp - command + 2, 15));
                port = atoi(temp + 1);
            }

            while (currentServer != NULL)
            {
                if (strcmp(address, currentServer->owner->ip) == 0 && port == currentServer->owner->port)
                {
                    /* if server output limit is reached, drop the client asking for invite 
                        (invite flood protection, kind of) */
                    if ((currentServer->owner->outBufCapacity - currentServer->owner->outBufLen) < 18)
                    {
                        wprintf(L"Server %hs reached maximum output, client %hs dropped\n", 
                            currentServer->owner->ip, client->ip);
                        return 0;
                    }

                    currentServer->owner->outBufLen += sprintf(
                        currentServer->owner->outBuf + currentServer->owner->outBufLen,
                        "x%s\n", client->ip) + 1;
                    currentServer->owner->writing = 1;

                    break;
                }

                currentServer = currentServer->next;
            }
        }
    }

    return 1;
}

// called each time any data has been received, looks for \r\n and calls onClientCommand if found
int onClientRead(planet_client *client)
{
    char buf[PLANET_MAX_INPUT];
    unsigned int i, lastCmd = 0;
    int err;

    if (client->inBufLen == PLANET_MAX_INPUT)
    {
        wprintf(L"Client %hs reached maximum input, dropped\n", client->ip);

        return 0;
    }
    err = recv(client->s, client->inBuf + client->inBufLen, PLANET_MAX_INPUT - client->inBufLen, 0);

#ifdef _DEBUG
    wprintf(L"Recv returned %d\n", err);
#endif

    /* drop the client on socket error */
    if (err <= 0)
    {
        return 0;
    }

    client->inBufLen += err;

    for (i = 0; i < (client->inBufLen - 1); i++)
    {
        if ((client->inBuf[i] == '\r') && (client->inBuf[i + 1] == '\n'))
        {
            memcpy(buf, client->inBuf + lastCmd, i - lastCmd);
            buf[i - lastCmd] = '\0';
            if (!onClientCommand(client, buf, i - lastCmd))
            {
                /* onClientCommand decided to close client connection, no further input parsing needed */
                return 0;
            }
            /* need to be able to process multiple commands in one receive, hence the lastCmd */
            lastCmd = i + 2;
        }
    }

    /* move the beginning of the next command to the start of buffer, if there was any */
    if (lastCmd != 0)
    {
        client->inBufLen -= lastCmd;
        memmove(client->inBuf, client->inBuf + lastCmd, client->inBufLen);
    }

    return 1;
}

/* main loop procedure, listens, polls and writes to the clients */
void planet()
{
    SOCKET serv;
    struct sockaddr_in addr;
    fd_set readSet;
    fd_set writeSet;
    int failCount = 0;

    serv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serv == INVALID_SOCKET)
    {
        wprintf(L"socket() failed\n");
        return;
    }

#ifdef _DEBUG
    wprintf(L"Serv socket created, = %d\n", serv);
#endif

    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PLANET_PORT);
    addr.sin_addr.IN_ADDR__S_ADDR = inet_addr(PLANET_HOST);

    if (bind(serv, (struct sockaddr*)&addr, sizeof(addr)) != 0)
    {
        wprintf(L"bind() failed\n");
        CLOSESOCKET(serv);
        return;
    }

#ifdef _DEBUG
    wprintf(L"Serv socket bound\n");
#endif

    if (listen(serv, SOMAXCONN) != 0)
    {
        wprintf(L"listen() failed\n");
        CLOSESOCKET(serv);
        return;
    }

#ifdef _DEBUG
    wprintf(L"Serv socket set to listen state\n");
#endif

    numberOfClients = 0;

    if (!LOCK_INIT)
	{
		wprintf(L"can't create lock mutex\n");
		CLOSESOCKET(serv);
		return;
	}

    while (1)
    {
        int maxfd;  /* lunix stuff */
        planet_client *currentClient, *nextClient;
        int isWriting = 0;

        FD_ZERO(&readSet);
        FD_ZERO(&writeSet);

        /* adding serv socket in poll set (if reported to be in reading state, this means the client has connected) */
        FD_SET(serv, &readSet);
#ifdef _DEBUG
        wprintf(L"Serv socket added to readSet = %d\n", serv);
#endif
        maxfd = (int)serv;
        
        /* adding client sockets in set */
        LOCK;
        currentClient = firstClient;
        while (currentClient != NULL)
        {
            if (currentClient->writing)
            {
#ifdef _DEBUG
                wprintf(L"Client socket %d added to writeSet\n", currentClient->s);
#endif
                FD_SET(currentClient->s, &writeSet);
                isWriting = 1;
            }
            else
            {
#ifdef _DEBUG
                wprintf(L"Client socket %d added to readSet\n", currentClient->s);
#endif
                FD_SET(currentClient->s, &readSet);
            }

            if ((int)currentClient->s > maxfd)
            {
                maxfd = (int)currentClient->s;
            }
            currentClient = currentClient->next;
        }
        UNLOCK;

        maxfd++;

#ifdef _DEBUG
        wprintf(L"Calling select, maxfd = %d\n", maxfd);
#endif
        if (select(maxfd, &readSet, (isWriting ? &writeSet : NULL), NULL, NULL) == SOCKET_ERROR)
        {
            /* sometimes select fails, but then restores. Ugly workaround */
            wprintf(L"select() failed (%d)\n", failCount);
            failCount += 1;
            if (failCount > 100)
                break;
            continue;
        }
        failCount = 0;

#ifdef _DEBUG
        wprintf(L"Returned from select\n");
#endif

        LOCK;
        /* reading/writing clients */
        for (currentClient = firstClient; currentClient != NULL; currentClient = nextClient)
        {
            /* remember next client here, because current client may be destroyed by the end of the loop */
            nextClient = currentClient->next;
            
            if (currentClient->writing)
            {
                if (FD_ISSET(currentClient->s, &writeSet))
                {
                    int err;

#ifdef _DEBUG
                    wprintf(L"Client socket %d is ready to write\n", currentClient->s);
#endif
                    err = send(currentClient->s, currentClient->outBuf, currentClient->outBufLen, MSG_NOSIGNAL);
                    if (err <= 0)
                    {
                        planetClientRemove(currentClient);
                        continue;
                    }

                    /* move what wasn't sent to the beginning of buffer */
                    currentClient->outBufLen -= err;
                    memmove(currentClient->outBuf, currentClient->outBuf + err, currentClient->outBufLen);
                    /* if everything has been sent, clear the writing flag */
                    if (currentClient->outBufLen == 0)
                        currentClient->writing = 0;
                }
            }
            else
            {
                if (FD_ISSET(currentClient->s, &readSet))
                {
#ifdef _DEBUG
                    wprintf(L"Client socket %d is ready to read\n", currentClient->s);
#endif
                    if (!onClientRead(currentClient))
                    {
                        planetClientRemove(currentClient);
                        continue;
                    }
                }
            }
        }
        
        /* looking for incoming connections */
        if (FD_ISSET(serv, &readSet))
        {
#ifdef _DEBUG
            wprintf(L"Serv socket is ready to read\n");
#endif
            onClientConnect(serv);
        }
        UNLOCK;
    }

    CLOSESOCKET(serv);
}

/* ping timeout-ing */
THREAD_RETURN_TYPE timerFunction(void *param)
{
	while (1)
	{
		planet_client *currentClient, *nextClient;
		time_t currentTime;

		time(&currentTime);

		LOCK;
		
		for (currentClient = firstClient; currentClient != NULL; currentClient = nextClient)
		{
			nextClient = currentClient->next;

			if ((currentTime - currentClient->lastPingTime) > CLIENT_PING_TIMEOUT)
			{
				wprintf(L"removing client %hs (ping timeout)\n", currentClient->ip);
				planetClientRemove(currentClient);
				continue;
			}
		}

		UNLOCK;

		SLEEP(10);
	}

	return 0;
}

#ifndef _WIN32
/* from the old planet. Don't know what all this is for */
void writepid()
{
    int pid = getpid();
    FILE *fpp;
    
    fpp = fopen("./nfkplanet.pid", "w");
    
    fprintf(fpp, "%d", pid);
    
    fclose(fpp);
}

void daemonize()
{
    pid_t pid=fork();
    
    if(pid == 0)
    {
        setsid();
        return;
    }
    
    if(pid < 0)
    {
        exit(666);
    }
    
    if(pid > 0)
    {
        exit(0);
    }
}
#endif

int main(int argc, char **argv)
{
#ifdef _WIN32
    WSADATA wd;
	HANDLE hThread;

    if (WSAStartup(0x0202, &wd) != 0)
    {
        wprintf(L"WSAStartup() failed\n");
		return 1;
    }
#else
	pthread_t thread;

    //daemonize();
    writepid();
#endif

#ifdef _WIN32
	hThread = (HANDLE)_beginthreadex(NULL, 0, timerFunction, NULL, 0, NULL);
	if (hThread == 0)
	{
		wprintf(L"can't start timer thread\n");
		return 1;
	}
	CloseHandle(hThread);
#else
	if (pthread_create(&thread, NULL, timerFunction, NULL))
	{
		wprintf(L"can't start timer thread\n");
		return 1;
	}
#endif

    planet();

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
