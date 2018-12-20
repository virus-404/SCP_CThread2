/* ---------------------------------------------------------------
Práctica 3.
Código fuente : gestor.c
Grau Informàtica
53399223P Agapito Gallart Bernat
--------------------------------------------------------------- */

#include "service.h"
#include "gestor.h"

#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>


#include <time.h>
#include <pthread.h>

#include <fcntl.h>           /* For O_* constants */
#include <sys/stat.h>        /* For mode constants */
#include <semaphore.h>

// globals
	char g_pwd[PATH_MAX+1];

  int sesion = 0;
	int signal_kill = 0;
	int max_threads;
	int C_stats = 0;
	int frequence_stats = 5;
	int flag_thread = -1;
	int last_thread_ID = 0;
	int server_socket;

	int* thread_list;
	int* socket_list;

	pthread_t* thread_workers;
	pthread_mutex_t privileges;
	pthread_mutex_t stop;
	pthread_cond_t cond;

	struct data* server_stats;

	struct timeval  time0;

	sem_t semaphore;

// functions handler

void sigchld_handler(int s)
{
	while(wait(NULL) > 0);
}

void sig_kill(int threads)
{
		fflush(stdout);
		printf("\n(!) WARNING SHUTDOWN (!)\n");
		close(server_socket);
		signal_kill = 1;
}

int main(int a_argc, char **ap_argv)
{
	// check args
		if(a_argc < 4)
		{
			printf("Usage: %s dir_name port_number\n", ap_argv[0]);
			return 1;
		}

	// variables
		max_threads = atoi(ap_argv[3]);
  	int serverSocket, clientSocket [max_threads], control_ID[max_threads];
  	socklen_t clientAddrSize;
		struct sockaddr_in clientAddr;
		struct sigaction signalAction;

		pthread_t thread_undertaker;
		pthread_t threads[max_threads];

		struct timeval time1;
		struct data server_data;
		server_stats = &server_data;

	// initialitzation vars
		realpath(ap_argv[1], g_pwd);

  // set globals variables
		socket_list = clientSocket;
		thread_list = control_ID;
		thread_workers = threads;
		gettimeofday(&time0, NULL);
		pthread_mutex_init(&privileges, NULL);
		pthread_mutex_init(&stop, NULL);
		sem_init(&semaphore, 0, max_threads);

	// set local variables

		server_stats->id = -1;
		server_stats->session = -1;
		server_stats->n_comands = 0;
		server_stats->n_gets = 0;
		server_stats->n_puts = 0;
		server_stats->b_get = 0;
		server_stats->b_put = 0;
		server_stats->t_session = 0;
		server_stats->t_get = 0;
		server_stats->t_put = 0;

		for (int x = 0; x < max_threads; x++) {
			control_ID[x] = -1;
		}

	// create service
		if(!service_create(&serverSocket, strtol(ap_argv[2], (char**)NULL, 10)))
		{
			fprintf(stderr, "%s: unable to create service on port: %s\n", ap_argv[0], ap_argv[2]);
			return 2;
		}
		server_socket = serverSocket;

	// setup termination handler
		signalAction.sa_handler = sigchld_handler; // reap all dead processes
		sigemptyset(&signalAction.sa_mask);
		signalAction.sa_flags = SA_RESTART;

		if (sigaction(SIGCHLD, &signalAction, NULL) == -1)
		{
			perror("main(): sigaction");
			return 3;
		}

		signal(SIGINT,sig_kill);

		#ifndef NOSERVERDEBUG
			flockfile(stdout);
			printf("\nmain(): waiting for clients...\n");
			funlockfile(stdout);
			pthread_create (&thread_undertaker, NULL, server_completion_service, NULL);

		#endif
	// dispatcher loop
		while(signal_kill == 0)
		{
			sem_wait(&semaphore); //Subreatc one instance of the semaphore
			clientAddrSize = sizeof(clientAddr);
			clientSocket[last_thread_ID] = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientAddrSize);
			if(clientSocket[last_thread_ID] != -1){
				#ifndef NOSERVERDEBUG
				  flockfile(stdout);
					printf("\nmain(): got client connection [addr=%s,port=%d] --> %d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), sesion);
					funlockfile(stdout);
				#endif
			  if(session_create(clientSocket[last_thread_ID])){
						void * tmp = &clientSocket[last_thread_ID];
						pthread_create (&threads[last_thread_ID], NULL, server_execution_service, tmp);
						#ifndef NOSERVERDEBUG
						  flockfile(stdout);
							printf("\nmain(): waiting for clients...\n");
							funlockfile(stdout);
						#endif
					}
			}
			for (int x = 0; x < max_threads; x++) {
				 if (control_ID[x] == -1) {
					 last_thread_ID = x;
					 x = max_threads;
				 }
			}
		}
	// destroy service
		fflush(stdout);
		close(serverSocket);
		gettimeofday(&time1, NULL);
		server_stats->t_session = time_difference(time1, time0);
		flockfile(stdout);
		show_statistics(*(server_stats));
		funlockfile(stdout);
		return 0;
}

Boolean service_create(int *ap_socket, const int a_port)
{
	// variables
		struct sockaddr_in serverAddr;

		#ifdef _PLATFORM_SOLARIS
			char yes='1';
		#else
			int yes=1;
		#endif

	// create address
		memset(&serverAddr, 0, sizeof(serverAddr));
		serverAddr.sin_family = AF_INET;
		serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
		serverAddr.sin_port = htons(a_port);

	// create socket
		if((*ap_socket = socket(serverAddr.sin_family, SOCK_STREAM, 0)) < 0)
		{
			perror("service_create(): create socket");
			return false;
		}

	// set options
		if(setsockopt(*ap_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0)
		{
			perror("service_create(): socket opts");
			return false;
		}

	// bind socket
		if(bind(*ap_socket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
		{
			perror("service_create(): bind socket");
			close(*ap_socket);
			return false;
		}

	// listen to socket
		if(listen(*ap_socket, SERVER_SOCKET_BACKLOG) < 0)
		{
			perror("service_create(): listen socket");
			close(*ap_socket);
			return false;
		}
	return true;
}

Boolean session_create(const int a_socket)
{
	// variables
		Message msgOut, msgIn;

	// initialitzation vars
		Message_clear(&msgOut);
		Message_clear(&msgIn);

	// session challenge dialogue

		// client: greeting
			if(!siftp_recv(a_socket, &msgIn) || !Message_hasType(&msgIn, SIFTP_VERBS_SESSION_BEGIN))
			{
				fprintf(stderr, "[%d] service_create(): session not requested by client.\n",sesion);
				return false;
			}

		// server: identify
		// client: username
			Message_setType(&msgOut, SIFTP_VERBS_IDENTIFY);

			if(!service_query(a_socket, &msgOut, &msgIn) || !Message_hasType(&msgIn, SIFTP_VERBS_USERNAME))
			{
				fprintf(stderr, "[%d] service_create(): username not specified by client.\n",sesion);
				return false;
			}

		// server: accept|deny
		// client: password
			Message_setType(&msgOut, SIFTP_VERBS_ACCEPTED); //XXX check username... not required for this project

			if(!service_query(a_socket, &msgOut, &msgIn) || !Message_hasType(&msgIn, SIFTP_VERBS_PASSWORD))
			{
				fprintf(stderr, "[%d] service_create(): password not specified by client.\n",sesion);
				return false;
			}

		// server: accept|deny
			if(Message_hasValue(&msgIn, SERVER_PASSWORD))
			{
				Message_setType(&msgOut, SIFTP_VERBS_ACCEPTED);
				siftp_send(a_socket, &msgOut);
			}
			else
			{
				Message_setType(&msgOut, SIFTP_VERBS_DENIED);
				siftp_send(a_socket, &msgOut);

				fprintf(stderr, "[%d] service_create(): client password rejected.\n",sesion);
				return false;
			}

		// session now established
			#ifndef NOSERVERDEBUG
				printf("[%d] session_create(): success\n",sesion);
			#endif

	return true;
}

void* server_execution_service(void * socket)
{
	// variables
		Message msg;
		String *p_argv;
		int argc;

		const int a_socket = *((int *) socket);
		struct data *statistics =  malloc (sizeof(struct data));
		struct timeval start, end;

	//fcntl(a_socket, F_SETFL, fcntl(a_socket, F_GETFL) | O_NONBLOCK);
	// initialitzation vars
		Message_clear(&msg);
		statistics->id = last_thread_ID;
		statistics->session = sesion;
		statistics->n_comands = 0;
		statistics->n_gets = 0;
		statistics->n_puts = 0;
		statistics->b_get = 0;
		statistics->b_put = 0;
		statistics->t_session = 0;
		statistics->t_get = 0;
		statistics->t_put = 0;

		sesion++;

		gettimeofday(&start, NULL);

	while(signal_kill == 0) // await request
	{
		siftp_recv(a_socket, &msg);
		if(Message_hasType(&msg, SIFTP_VERBS_SESSION_END) || Message_hasType(&msg, "")){
			break;
		}else {
			#ifndef NOSERVERDEBUG
				printf("[%d] service_loop(): got command '%s'\n",statistics->session, Message_getValue(&msg));
			#endif

			// parse request
				if((p_argv = service_parseArgs(Message_getValue(&msg), &argc)) == NULL || argc <= 0)
				{
					service_freeArgs(p_argv, argc);

					if(!service_sendStatus(a_socket, false)) // send negative ack
						break;
					continue;
				}

			// handle request
				if(!server_CMD_executor(a_socket, p_argv, argc, statistics))
					service_sendStatus(a_socket, false); // send negative ack upon fail

			// clean up
				service_freeArgs(p_argv, argc);
				p_argv = NULL;
		}
	}

	pthread_mutex_lock(&stop);
	flag_thread = statistics->id;
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&stop);

	gettimeofday(&end, NULL);
	statistics->t_session = time_difference(end, start);
	pthread_exit(statistics);
}

void* server_completion_service(void * socket)
{
	if (pthread_cond_init(&cond, NULL) != 0) perror("pthread_cond_init() error");
	struct data* data_client;
	while (true) {
		pthread_mutex_lock(&stop);
		while (flag_thread == -1){
			if (signal_kill == 1) pthread_exit(NULL);
			pthread_cond_wait(&cond, &stop);
		}
		session_destroy(*(socket_list + flag_thread));
		sem_post (&semaphore);
		pthread_join(*(thread_workers + flag_thread), (void*) &data_client);
		*(thread_list + flag_thread) = -1;
		flockfile(stdout);
		show_statistics(*(data_client));
		funlockfile(stdout);
		free(data_client);
		close(*(socket_list + flag_thread)); // parent doesn't need this socket}
		flag_thread = -1;
		pthread_mutex_unlock(&stop);
	}
	return NULL;
}

Boolean server_CMD_executor(const int a_socket, const String *ap_argv, const int a_argc, struct data *statistics)
{
	// variables
		Message msg;

		String dataBuf;
		int dataBufLen;

		Boolean tempStatus = false;
		fflush(stdout);
		flockfile(stdout);
		update_counter();
		funlockfile(stdout);
    struct timeval start, end;

	// initialitzation variables
		Message_clear(&msg);

	if(strcmp(ap_argv[0], "ls") == 0)
	{
    statistics->n_comands++;
		pthread_mutex_lock (&privileges);
		server_stats->n_comands ++;

		if((dataBuf = service_readDir(g_pwd, &dataBufLen)) != NULL)
		{
			// transmit data
				if(service_sendStatus(a_socket, true))
					tempStatus = siftp_sendData(a_socket, dataBuf, dataBufLen);

				#ifndef NOSERVERDEBUG
					printf("[%d] ls(): status=%d\n",statistics->session, tempStatus);
				#endif

			// clean up
				free(dataBuf);
			pthread_mutex_unlock (&privileges);
			return tempStatus;
		}
	}

	else if(strcmp(ap_argv[0], "pwd") == 0)
	{
    statistics->n_comands++;
		pthread_mutex_lock (&privileges);
		server_stats->n_comands ++;
		if(service_sendStatus(a_socket, true)){
			tempStatus = siftp_sendData(a_socket, g_pwd, strlen(g_pwd));
			pthread_mutex_unlock (&privileges);
			return tempStatus;
		}else{
				pthread_mutex_unlock (&privileges);
		}
	}

	else if(strcmp(ap_argv[0], "cd") == 0 && a_argc > 1)
	{
    statistics->n_comands++;
		pthread_mutex_lock (&privileges);
		server_stats->n_comands ++;
		pthread_mutex_unlock (&privileges);
		tempStatus = service_sendStatus(a_socket, service_handleCmd_chdir(g_pwd, ap_argv[1]));
		pthread_mutex_unlock (&privileges);
		return tempStatus;

	}

	else if(strcmp(ap_argv[0], "get") == 0 && a_argc > 1)
	{
    statistics->n_comands++;
		pthread_mutex_lock (&privileges);
		server_stats->n_comands ++;
		statistics->n_gets++;
		server_stats->n_gets ++;
		char srcPath[PATH_MAX+1];

		// determine absolute path
		if(service_getAbsolutePath(g_pwd, ap_argv[1], srcPath))
		{
			// check read perms & file type
			if(service_permTest(srcPath, SERVICE_PERMS_READ_TEST) && service_statTest(srcPath, S_IFMT, S_IFREG))
			{
				// read file

				if((dataBuf = service_readFile(srcPath, &dataBufLen)) != NULL)
				{
					if(service_sendStatus(a_socket, true))
					{
						// send file
            gettimeofday(&start, NULL);
						tempStatus = siftp_sendData(a_socket, dataBuf, dataBufLen);
            gettimeofday(&end, NULL);
            statistics->t_get = statistics->t_get + time_difference (end, start);
						server_stats->t_get = server_stats->t_get + time_difference(end,start);
            statistics->b_get = dataBufLen/1000 + statistics->b_get;
						server_stats->b_get = dataBufLen/1000  + server_stats->b_get;
						#ifndef NOSERVERDEBUG
							printf("[%d] get(): file sent %s.\n",statistics->session, tempStatus ? "OK" : "FAILED");
						#endif
					}
					#ifndef NOSERVERDEBUG
					else
						printf("[%d] get(): remote host didn't get status ACK.\n",statistics->session);
					#endif

					free(dataBuf);
				}
				#ifndef NOSERVERDEBUG
				else
					printf("[%d]get(): file reading failed.\n",statistics->session);
				#endif
			}
			#ifndef NOSERVERDEBUG
			else
				printf("[%d]get(): don't have read permissions.\n",statistics->session);
			#endif
		}
		#ifndef NOSERVERDEBUG
		else
			printf("[%d] get(): absolute path determining failed.\n",statistics->session);
		#endif
		pthread_mutex_unlock (&privileges);
		return tempStatus;
	}

	else if(strcmp(ap_argv[0], "put") == 0 && a_argc > 1)
	{
    statistics->n_comands++;
		pthread_mutex_lock (&privileges);
		server_stats->n_comands ++;
    statistics->n_puts++;
		server_stats->n_puts++;
		char dstPath[PATH_MAX+1];

		// determine destination file path
		if(service_getAbsolutePath(g_pwd, ap_argv[1], dstPath))
		{
			// check write perms & file type
			if(service_permTest(dstPath, SERVICE_PERMS_WRITE_TEST) && service_statTest(dstPath, S_IFMT, S_IFREG))
			{
				// send primary ack: file perms OK
				if(service_sendStatus(a_socket, true))
				{
					// receive file
					if((dataBuf = siftp_recvData(a_socket, &dataBufLen)) != NULL)
					{
						#ifndef NOSERVERDEBUG
							printf("[%d] put(): about to write to file '%s'\n",statistics->session, dstPath);
						#endif

            gettimeofday(&start, NULL);

						tempStatus = service_writeFile(dstPath, dataBuf, dataBufLen);

            gettimeofday(&end, NULL);;
            statistics->t_put = statistics->t_put + time_difference(end,start);
						server_stats->t_put = server_stats->t_put + time_difference(end,start);
            statistics->b_put = dataBufLen/1000  + statistics->b_put;
						server_stats->b_put = dataBufLen/1000  + server_stats->b_put;
						free(dataBuf);

						#ifndef NOSERVERDEBUG
							printf("[%d] put(): file writing %s.\n", statistics->session, tempStatus ? "OK" : "FAILED");
						#endif
						pthread_mutex_unlock (&privileges);
						// send secondary ack: file was written OK
						if(tempStatus)
						{
							return service_sendStatus(a_socket, true);
						}
					}
					#ifndef NOSERVERDEBUG
					else
						printf("[%d] put(): getting of remote file failed.\n",statistics->session);
					#endif
				}
				#ifndef NOSERVERDEBUG
				else
					printf("[%d] put(): remote host didn't get status ACK.\n",statistics->session);
				#endif
			}
			#ifndef NOSERVERDEBUG
			else
				printf("[%d] put(): don't have write permissions.\n",statistics->session);
			#endif
		}
		#ifndef NOSERVERDEBUG
		else
			printf("[%d] put(): absolute path determining failed.\n",statistics->session);
		#endif
	}

	else if (strcmp(ap_argv[0], "exit") == 0){
		statistics->n_comands++;
		pthread_mutex_lock (&privileges);
		server_stats->n_comands ++;
		pthread_mutex_unlock (&privileges);
		return true;
	}
	return false;
}

void show_statistics(struct data statistics){
	printf("--------------------------\n");
	printf("---[ID]Statistic: value---\n");
	printf("--------------------------\n");
	printf("[%d]Session time: %f\n", statistics.session, statistics.t_session);
	printf("[%d]Number commands: %i\n", statistics.session, statistics.n_comands);
	printf("[%d]Commmand per minute: %f\n", statistics.session, (60 * statistics.n_comands)/statistics.t_session);
	printf("[%d]Number of uploads: %i\n", statistics.session, statistics.n_puts);
	printf("[%d]Upload velocity (kb): %f\n", statistics.session, statistics.b_put/statistics.t_put);
	printf("[%d]Number of downloads: %i\n", statistics.session, statistics.n_gets);
	printf("[%d]Downloads velocity (kb): %f\n", statistics.session, statistics.b_get/statistics.t_get);
	printf("--------------------------\n");
}

void update_counter() {
	if (C_stats  + 1 == frequence_stats){
		show_real_time_statistics();
		C_stats = 0;
	} else C_stats++;
}

void show_real_time_statistics() {
	struct timeval time1;
	gettimeofday(&time1, NULL);

	printf("--------------------------\n");
	printf("--[REAL TIME STATISTICS]--\n");
	printf("--------------------------\n");
	printf("[RT]Session time: %f\n", time_difference(time1, time0));
	printf("[RT]Number commands: %i\n", server_stats->n_comands);
	printf("[RT]Commmand per minute: %f\n", (60 * server_stats->n_comands)/time_difference(time1, time0));
	printf("[RT]Number of uploads: %i\n", server_stats->n_puts);
	printf("[RT]Upload velocity (kb): %f\n", server_stats->b_put/server_stats->t_put);
	printf("[RT]Number of downloads: %i\n", server_stats->n_gets);
	printf("[RT]Downloads velocity (kb): %f\n", server_stats->b_get/server_stats->t_get);
	printf("--------------------------\n");
}

double time_difference(struct timeval time1, struct timeval time0){
	return (double) (time1.tv_usec - time0.tv_usec) / 1000000 + (double)(time1.tv_sec - time0.tv_sec);
}
