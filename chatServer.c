#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>

#include <signal.h>
#include <sys/ioctl.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "chatServer.h"

#define ERROR    -1

static int end_server = 0;

void intHandler(int SIG_INT) {
	/* use a flag to end_server to break the main loop */
	end_server = 1;
	return;
}


int clean_up(conn_pool_t *pool)
{
	if (pool == NULL)
	{
		return -1;
	}

	for (struct conn *curptr = pool->conn_head; curptr != NULL;)
	{
		printf("Removing connection with sd %d \n", curptr->fd);
		shutdown(curptr->fd, SHUT_RDWR);
		close(curptr->fd);

		for (struct msg *msgptr = curptr->write_msg_head; msgptr != NULL;)
		{
			if (msgptr->message != NULL)
			{
				free(msgptr->message);
			}

			struct msg *delmsgptr = msgptr;
			msgptr = msgptr->next;
			free(delmsgptr);
		}

		struct conn *delptr = curptr;
		curptr = curptr->next;
		free(delptr);
	}

	free(pool);

	return 0;
}

int main(int argc, char *argv[])
{
	/* Check command line arguments */
	if (argc != 2)
	{
		char errMsg[100];
		snprintf(errMsg, sizeof(errMsg), "Usage: %s <listen-port>", argv[0]);
		perror(errMsg);
		exit(EXIT_SUCCESS);
	}

	long int port;
	port = strtol(argv[1], NULL, 0);
	if (port <= 0)
	{
		perror("Incorrect port");
		exit(EXIT_SUCCESS);
	}

	signal(SIGINT, intHandler);
	signal(SIGTERM, intHandler);

	/*************************************************************/
	/* Create an AF_INET stream socket to receive incoming      */
	/* connections on                                            */
	/*************************************************************/
	int masterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (ERROR == masterSocket)
	{
		perror("create socket");
		exit(EXIT_FAILURE);
	}

	/*************************************************************/
	/* Set socket to be nonblocking. All of the sockets for      */
	/* the incoming connections will also be nonblocking since   */
	/* they will inherit that state from the listening socket.   */
	/*************************************************************/
	int ret;
#if defined(O_NONBLOCK)
	ret = fcntl(masterSocket, F_GETFL, 0);
	if (ERROR != ret)
	{
		ret = fcntl(masterSocket, F_SETFL, ret | O_NONBLOCK);
	}
#else
	int flags = 1;
	ret = ioctl(masterSocket, FIONBIO, &flags);
#endif
	if (ERROR == ret)
	{
		perror("set socket nonbloking");
		exit(EXIT_FAILURE);
	}

	int value = 1;
	if (ERROR == setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)))
	{
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}

	/*************************************************************/
	/* Bind the socket                                           */
	/*************************************************************/
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(struct sockaddr_in));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);

	if (ERROR == bind(masterSocket, (struct sockaddr *)&sa, sizeof(struct sockaddr_in)))
	{
		perror("bind");
		exit(EXIT_FAILURE);
	}

	/*************************************************************/
	/* Set the listen back log                                   */
	/*************************************************************/
	if (ERROR == listen(masterSocket, SOMAXCONN))
	{
		perror("listen");
		exit(EXIT_FAILURE);
	}

	conn_pool_t *pool = malloc(sizeof(conn_pool_t));
	init_pool(pool);

	/*************************************************************/
	/* Initialize fd_sets  			                             */
	/*************************************************************/
	// pool->maxfd = masterSocket;
	// FD_SET(masterSocket, &pool->read_set);

	/*************************************************************/
	/* Loop waiting for incoming connects, for incoming data or  */
	/* to write data, on any of the connected sockets.           */
	/*************************************************************/
	do
	{
		/**********************************************************/
		/* Copy the master fd_set over to the working fd_set.     */
		/**********************************************************/
		fd_set readfds, writefds;
		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		readfds = pool->read_set;
		writefds = pool->write_set;
		FD_SET(masterSocket, &readfds);
		int maxSocket = masterSocket > pool->maxfd ? masterSocket : pool->maxfd;

		/**********************************************************/
		/* Call select() 										  */
		/**********************************************************/
		printf("Waiting on select()...\nMaxFd %d\n", pool->maxfd);
		if (ERROR == select(maxSocket + 1, &readfds, &writefds, NULL, NULL))
		{
			perror("select");
			break;
		}

		static char buffer[1024];
		memset(buffer, 0, sizeof(buffer));

		if (FD_ISSET(masterSocket, &readfds))
		{
			int sd = accept(masterSocket, NULL, NULL);
			if (ERROR == sd)
			{
				/* ignore in the accordance to the task formulation */
			}
			else
			{
				add_conn(sd, pool);
			}
		}

		/**********************************************************/
		/* One or more descriptors are readable or writable.      */
		/* Need to determine which ones they are.                 */
		/**********************************************************/
		for (struct conn *ptr = pool->conn_head; ptr != NULL;)
		{
			int wasRemoved = 0; /* flag for safe processing */

			/*******************************************************/
			/* Check to see if this descriptor is ready for read   */
			/*******************************************************/
			if (FD_ISSET(ptr->fd, &readfds))
			{
				printf("Descriptor %d is readable\n", ptr->fd);

				int len = recv(ptr->fd, buffer, sizeof(buffer), MSG_NOSIGNAL);

				printf("%d bytes received from sd %d\n", len, ptr->fd);

				if (ERROR == len)
				{
					/* skip the operation and continue 
					   in the accordance to the task formulation */
				}
				else if (0 == len)
				{
					struct conn *prevptr = ptr->prev;

					printf("Connection closed for sd %d\n", ptr->fd);
					remove_conn(ptr->fd, pool);

					/* ptr is no longer valid after remove_conn(...) */
					ptr = prevptr;
					if (ptr == NULL)
					{
						/* head was changed */
						ptr = pool->conn_head;
					}
					else
					{
						ptr = ptr->next;
					}

					wasRemoved = 1;
				}
				else
				{
					add_msg(ptr->fd, buffer, len, pool);
				}
			} /* End of if (FD_ISSET()) */

			if (!wasRemoved)
			{
				/*******************************************************/
				/* Check to see if this descriptor is ready for write  */
				/*******************************************************/
				if (FD_ISSET(ptr->fd, &writefds))
				{
					write_to_client(ptr->fd, pool);
				}

				ptr = ptr->next;
			}

		} /* End of loop through selectable descriptors */
	} while (!end_server);

	/*************************************************************/
	/* If we are here, Control-C was typed,						 */
	/* clean up all open connections					         */
	/*************************************************************/

	clean_up(pool);
	close(masterSocket);

	return 0;
}

int init_pool(conn_pool_t *pool)
{
	if (pool == NULL)
	{
		return -1;
	}

	pool->maxfd = -1;
	pool->nready = 0;
	FD_ZERO(&pool->read_set);
	FD_ZERO(&pool->ready_read_set);
	FD_ZERO(&pool->write_set);
	FD_ZERO(&pool->ready_write_set);
	pool->conn_head = NULL;
	pool->nr_conns = 0;

	return 0;
}

int add_conn(int sd, conn_pool_t *pool)
{
	printf("New incoming connection on sd %d\n", sd);

	if (pool == NULL)
	{
		return -1;
	}

	if (sd > pool->maxfd)
	{
		pool->maxfd = sd;
	}

	FD_SET(sd, &pool->read_set);

	struct conn *newItem = malloc(sizeof(conn_t));
	memset(newItem, 0, sizeof(conn_t));
	newItem->fd = sd;

	if (pool->conn_head == NULL)
	{
		pool->conn_head = newItem;
	}
	else
	{
		struct conn *ptr = pool->conn_head;

		while (ptr->next != NULL)
		{
			ptr = ptr->next;
		}
		newItem->prev = ptr;
		ptr->next = newItem;
	}

	pool->nr_conns++;

	return 0;
}

int remove_conn(int sd, conn_pool_t *pool)
{
	printf("Removing connection with sd %d \n", sd);
		
	if (pool == NULL)
	{
		return -1;
	}

	struct conn *ptr;
	for (ptr = pool->conn_head; ptr != NULL && ptr->fd != sd; ptr = ptr->next)
		;

	if (NULL == ptr)
	{
		return -1;
	}

	for (struct msg *msgptr = ptr->write_msg_head; msgptr != NULL;)
	{
		if (msgptr->message != NULL)
		{
			free(msgptr->message);
		}

		struct msg *delmsgptr = msgptr;
		msgptr = msgptr->next;
		free(delmsgptr);
	}

	if (ptr->prev != NULL)
	{
		ptr->prev->next = ptr->next;
	}
	else
	{
		pool->conn_head = ptr->next;
	}
	if (ptr->next != NULL)
	{
		ptr->next->prev = ptr->prev;
	}

	free(ptr);

	FD_CLR(sd, &pool->read_set);
	FD_CLR(sd, &pool->write_set);

	pool->nr_conns--;

	if (pool->maxfd == sd)
	{
		pool->maxfd = -1;
		for (struct conn *ptr = pool->conn_head; ptr != NULL; ptr = ptr->next)
		{
			if (ptr->fd > pool->maxfd)
			{
				pool->maxfd = ptr->fd;
			}
		}
	}

	return 0;
}

int add_msg(int sd, char *buffer, int len, conn_pool_t *pool)
{
	if ((buffer == NULL) || (len == 0) || (pool == NULL))
	{
		return -1;
	}

	for (struct conn *ptr = pool->conn_head; ptr != NULL; ptr = ptr->next)
	{
		if (ptr->fd != sd)
		{
			struct msg *msgItem = malloc(sizeof(msg_t));
			memset(msgItem, 0, sizeof(msg_t));

			msgItem->message = malloc(len * sizeof(char));
			strncpy(msgItem->message, buffer, len);
			msgItem->size = len;

			if (ptr->write_msg_tail == NULL)
			{
				ptr->write_msg_head = ptr->write_msg_tail = msgItem;
			}
			else
			{
				ptr->write_msg_tail->next = msgItem;
				msgItem->prev = ptr->write_msg_tail;
				ptr->write_msg_tail = msgItem;
			}

			FD_SET(ptr->fd, &pool->write_set);
		}
	}

	return 0;
}

int write_to_client(int sd, conn_pool_t *pool)
{
	if (pool == NULL)
	{
		return -1;
	}

	struct conn *ptr;
	for (ptr = pool->conn_head; ptr != NULL && ptr->fd != sd; ptr = ptr->next)
		;

	if (NULL == ptr)
	{
		return -1;
	}

	struct msg *msgptr = ptr->write_msg_head;
	if (msgptr == NULL)
	{
		return -1;
	}

	while (msgptr != NULL)
	{
		if (ERROR == send(sd, msgptr->message, msgptr->size, MSG_NOSIGNAL))
		{
			/* skip the operation and continue 
		   in the accordance to the task formulation */
		}

		free(msgptr->message);

		struct msg *delmsgptr = msgptr;
		msgptr = msgptr->next;
		free(delmsgptr);
	}

	ptr->write_msg_head = ptr->write_msg_tail = NULL;

	FD_CLR(sd, &pool->write_set);

	return 0;
}
