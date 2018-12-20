/**
 * Suraj Kurapati <skurapat@ucsc.edu>
 * CMPS-150, Spring04, final project
 *
 * SimpleFTP server interface.
**/

#ifndef SERVER_H
#define SERVER_H

#include "siftp.h"
#include <sys/time.h>

	/* constants */

		#define SERVER_SOCKET_BACKLOG	5
		#define SERVER_PASSWORD	"scp18"

//		#define CONCURRENT 1

	/* services */

		/**
		 * Establishes a network service on the specified port.
		 * @param	ap_socket	Storage for socket descriptor.
		 * @param	a_port	Port number in decimal.void update_counter()
		 */
		Boolean service_create(int *ap_socket, const int a_port);
		Boolean server_CMD_executor(const int a_socket, const String *ap_argv, const int a_argc, struct data *statistics);
    int get_lazy_thread(int list_threads []);
		int finished_client_getter();
		void* server_execution_service(void * socket);
		void* server_completion_service(void * socket);
		void show_statistics(struct data statistics);
		void show_real_time_statistics();
		void update_counter();
		double time_difference(struct timeval time1, struct timeval time0);

#endif
