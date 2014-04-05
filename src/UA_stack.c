/*
 * UA_stack.c
 *
 *  Created on: 04.04.2014
 *      Author: mrt
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/socketvar.h>

#include <errno.h> // errno, EINTR

#include <pthread.h>

#include "UA_stack.h"

UA_TL_Description UA_TransportLayerDescriptorTcpBinary  = {
		UA_TL_ENCODING_BINARY,
		UA_TL_CONNECTIONTYPE_TCPV4,
		UA_TL_MAXCONNECTIONS_DEFAULT,
		{-1,8192,8192,16384,1}
};

// TODO: do we really need a variable global to the module?
UA_TL_data theTL;


/** checks arguments and dispatches to worker or refuses to init */
UA_Int32 UA_TL_init(UA_TL_Description* tlDesc, UA_Int32 port) {
	UA_Int32 retval = UA_SUCCESS;
	if (tlDesc->connectionType == UA_TL_CONNECTIONTYPE_TCPV4 && tlDesc->encoding == UA_TL_ENCODING_BINARY) {
		theTL.tld = &tlDesc;
		UA_list_init(&theTL.connections);
		retval |= UA_TL_TCP_init(theTL,port);
	} else {
		retval = UA_ERR_NOT_IMPLEMENTED;
	}
	return retval;
}


/** the tcp reader thread **/
void* UA_TL_TCP_reader(void *p) {
	UA_TL_connection* c = (UA_TL_connection*) p;

	UA_ByteString readBuffer;
	UA_alloc(&(readBuffer.data),c->localConf.recvBufferSize);

	while (c->connectionState != connectionState_CLOSE) {
		readBuffer.length = read(c->socket, readBuffer.data, c->localConf.recvBufferSize);

		UA_ByteString_printx("server_run - received=",&readBuffer);

		if (readBuffer.length  > 0) {
			TL_process(&c,&readBuffer);
		} else if (readBuffer.length < 0) {
			perror("ERROR reading from socket1");
			break;
		}
	}
	// clean up: socket, buffer, connection
	// not really necessary?
	c->connectionState = connectionState_CLOSE;
	// free resources allocated with socket
	close(c->socket);
	UA_ByteString_deleteMembers(&readBuffer);
	// FIXME: C has no lambdas, we need a matcher with two arguments
	// UA_list_Element* lec = UA_list_findFirst(theTL,c,compare);
	// TODO: can we get get rid of reference to theTL?
	UA_list_Element* lec = UA_list_find(&theTL.connections,UA_NULL);
	UA_list_removeElement(lec,UA_NULL);
	UA_free(lec->payload);
	return UA_NULL;
}

/** write to a tcp transport layer connection */
void* UA_TL_TCP_write(UA_TL_connection* c, UA_ByteString* msg) {
	UA_ByteString_printx("write data:", msg);
	int nWritten = 0;
	while (nWritten < msg->length) {
		int n;
		do {
			n = write(c->socket, &(msg->data[n]), msg->length-n);
		} while (n == -1L && errno == EINTR);
		if (n >= 0) {
			nWritten += n;
		} else {
			// TODO: error handling
		}
	}
	return UA_NULL;
}

/** the tcp listener thread **/
void* UA_TL_TCP_listen(void *p) {
	UA_TL_data* tld = (UA_TL_data*) p;

	UA_String_printf("open62541-server at ",&(tld->endpointUrl));
	while (UA_TRUE) {
		listen(tld->listenerHandle, tld->tld->maxConnections);

		// accept only if not max number of connections exceeded
		if (tld->tld->maxConnections == -1 || tld->connections.size < tld->tld->maxConnections) {
			struct sockaddr_in cli_addr;
			int cli_len = sizeof(cli_addr);
			int newsockfd = accept(tld->listenerHandle, (struct sockaddr *) &cli_addr, &cli_len);
			if (newsockfd < 0) {
				perror("ERROR on accept");
			} else {
				UA_TL_connection* c;
				UA_Int32 retval = UA_SUCCESS;
				retval |= UA_alloc(&c,sizeof(UA_TL_connection));
				TL_Connection_init(c, tld);
				c->socket = newsockfd;
				c->UA_TL_writer = UA_TL_TCP_write;
				// add to list
				UA_list_addPayloadToBack(&(tld->connections),c);
				// TODO: handle retval of pthread_create
				pthread_create( &(c->readerThread), NULL, UA_TL_TCP_reader, (void*) c);
			}
		} else {
			// no action necessary to reject connection
		}
	}
	return UA_NULL;
}

UA_Int32 UA_TL_TCP_init(UA_TL_data* tld, UA_Int32 port) {
	UA_Int32 retval = UA_SUCCESS;
	// socket variables
	int sockfd;
	int optval = 1;
	struct sockaddr_in serv_addr;

	// create socket for listening to incoming connections
	tld->listenerHandle = socket(PF_INET, SOCK_STREAM, 0);
	if (tld->listenerHandle < 0) {
		perror("ERROR opening socket");
		retval = UA_ERROR;
	} else {
		// set port number, options and bind
		memset((void *) &serv_addr, sizeof(serv_addr),1);
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_addr.s_addr = INADDR_ANY;
		serv_addr.sin_port = htons(port);
		if (setsockopt(tld->listenerHandle, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval)
				== -1) {
			perror("setsockopt");
			retval = UA_ERROR;
		} else {
			// bind to port
			if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
				perror("ERROR on binding");
				retval = UA_ERROR;
			} else {
				// TODO: implement
				// UA_String_sprintf("opc.tpc://localhost:%d", &(tld->endpointUrl), port);
			}
		}
	}
	// finally
	if (retval == UA_SUCCESS) {
		// TODO: handle retval of pthread_create
		pthread_create( &tld->listenerThreadHandle, NULL, UA_TL_TCP_listen, (void*) tld);
	}
	return retval;
}

/** checks arguments and dispatches to worker or refuses to init */
UA_Int32 UA_Stack_init(UA_TL_Description* tlDesc, UA_Int32 port) {
	UA_Int32 retval = UA_SUCCESS;
	retval = UA_TL_init(tlDesc,port);
	return retval;
}