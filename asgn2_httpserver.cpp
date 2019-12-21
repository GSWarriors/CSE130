#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

void parsePut(int strDividerSize, char **strArray, char *fileName, int new_conn);
void parseGet(int strDividerSize, char **strArray, char *fileName, int new_conn);
void handleGet(char *fileName, int new_conn);
void handlePut(char *fileName, int new_conn, int contentSize);
int checkContentLength(char **strArray);
int validName(char *fileName, int new_conn);
void *assignThread(void *new_connection);
void *handleClients(void *);
void parseRequest(int new_connection, char *inputBuffer, int bufferSize);

/*variables for thread synchronization and thread pool*/
int pool_size;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock;
pthread_mutex_t dispatch_lock;
int busy = 0;

/*struct for queue*/
struct entry {
	char *clientReq;
	int my_connection;

	TAILQ_ENTRY(entry) entries;
};

/*tail queue requires a head*/
TAILQ_HEAD(, entry) tail_head;

int main(int argc, char *argv[])
{
	/*address and port are specified on command line for server to listen to*/

	/*socket takes domain, type, and protocol, uses sockaddr_in*/

	int PORT;
	int serverSocket;
	struct sockaddr_in *currentAddress;
	int bindSocket;
	int socketListener;
	int addressLen;
	char *myAddress;
	int numThreads = 0;

	/*command line inputs, for only IP address, and for IP address and PORT*/

	if(argc == 2) {
		numThreads = 4;
		myAddress = argv[1];
		PORT = 80;
	} else if(argc == 4) {
		numThreads = atoi(argv[2]);
		myAddress = argv[3];
		PORT = 80;
	} else if(argc == 5) {
		numThreads = atoi(argv[2]);
		myAddress = argv[3];
		PORT = atoi(argv[4]);
	}

	pool_size = numThreads;

	serverSocket = socket(AF_INET, SOCK_STREAM, 0);

	if(serverSocket < 0) {
		perror("can't create socket");
		return -1;
	}

	currentAddress = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));

	memset((char *)currentAddress, 0, sizeof(currentAddress));

	/*assign address to sockaddr_in type currentAddress*/
	currentAddress->sin_family = AF_INET;
	currentAddress->sin_port = htons(PORT);

	inet_pton(AF_INET, myAddress, &currentAddress->sin_addr);

	/*now, need to assign a port number to the socket*/
	bindSocket = bind(serverSocket, (struct sockaddr *)currentAddress, sizeof(struct sockaddr_in));

	if(bindSocket < 0) {
		perror("can't bind socket to given address");
		return -1;
	}

	/*implementing listen to listen to incoming commands from client*/
	socketListener = listen(serverSocket, 10);

	if(socketListener < 0) {
		perror("connection refused or socket can't accept connections");
		return -1;
	}

	addressLen = sizeof(currentAddress);

	/*initialize mutexes*/

	if(pthread_mutex_init(&lock, NULL) != 0) {
		perror("Mutex initalization failed");
		return -1;
	}

	if(pthread_mutex_init(&dispatch_lock, NULL) != 0) {
		perror("Dispatch mutex init failed");
		return -1;
	}

	/*initializes dispatch thread, thread pool, and timeout struct*/
	pthread_t detect_connection;
	pthread_t thread_pool[numThreads];
	int thread_num[numThreads];
	int i = 0;
	struct timeval timeout;

	/*creates thread pool that waits on condition variable*/
	for(i = 0; i < numThreads; i++) {
		thread_num[i] = i;
		pthread_create(&thread_pool[i], NULL, handleClients, (void *)&i);
		printf("creating thread with id: %d\n", i);
	}

	sleep(5);

	/*initialize tail queue*/
	TAILQ_INIT(&tail_head);

	timeout.tv_sec = 10;
	timeout.tv_usec = 0;

	/*while loop to keep connection open for incoming requests from client.
	 * 	 Sets timeout option for hanging client requests, and creates dispatch thread for each request*/
	while(1) {
		int new_connection =
			accept(serverSocket, (struct sockaddr *)currentAddress, (socklen_t *)&addressLen);

		if(new_connection < 0) {
			perror("socket not accepted");
			return -1;
		}

		if(setsockopt(new_connection, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout))
				< 0) {
			perror("setsockopt failed");
			return -1;
		}


		if(pthread_create(&detect_connection, NULL, &assignThread, (void *)&new_connection) < 0) {
			perror("can't create a new dispatch thread");
			return -1;
		}

		sleep(10);
		/*wait to close the connection (want it to process)*/
		close(new_connection);
	}

	return 0;
}

/*moved request parsing from within main() to separate function parseRequest.
 * Takes care of parsing GET and PUT requests*/

void parseRequest(int new_conn, char *inputBuffer, int bufferSize)
{
	char delim[] = " ";
	char *strDivider;

	/*tokenizes string from inputBuffer using " " as a delimiter*/
	strDivider = strtok(inputBuffer, delim);

	int strDividerSize = sizeof(strDivider);

	char *strArray[strDividerSize];

	int i = 0;

	while(strDivider != NULL) {
		strArray[i++] = strDivider;
		strDivider = strtok(NULL, delim);
	}

	if(strcmp(strArray[0], "GET") == 0) {
		strDividerSize -= 2;
	}

	char const *putStr = "PUT";
	char const *getStr = "GET";
	char *fileName = NULL;
	int putRequest = 0;
	int getRequest = 0;

	/*loop to check whether input is a PUT or GET request*/
	for(i = 0; i < strDividerSize; i++) {
		if(strstr(strArray[i], putStr) != NULL) {
			putRequest = 1;
		} else if(strstr(strArray[i], getStr) != NULL) {
			getRequest = 1;
		}
	}

	/*parses the request based on which one is detected*/
	if(putRequest == 1) {
		parsePut(strDividerSize, strArray, fileName, new_conn);
	} else if(getRequest == 1) {
		parseGet(strDividerSize, strArray, fileName, new_conn);
	}

	/*writes to STDOUT whatever is in the input buffer*/
	write(STDOUT_FILENO, inputBuffer, bufferSize);
}


/*Worker thread function. Conditionally waits for dispatch thread to signal.
 * Once signalled, dequeues request and sends request info to parseRequest*/
void *handleClients(void *)
{
	while(1) {
		pthread_mutex_lock(&lock);

		while(!busy) {
			/*printf("thread is currently blocked, waiting\n");*/
			pthread_cond_wait(&cond, &lock);
		}

		struct entry *my_item;

		if(!TAILQ_EMPTY(&tail_head)) {
			my_item = TAILQ_FIRST(&tail_head);
			parseRequest(my_item->my_connection, my_item->clientReq, sizeof(my_item->clientReq));

			TAILQ_REMOVE(&tail_head, my_item, entries);
			free(my_item);
		}

		busy = 0;
		pthread_mutex_unlock(&lock);
	}

	return NULL;
}

/*Dispatch thread function. Reads request from client socket and adds it to the queue
 * Signals one of the worker threads to handle queued requests.*/

void *assignThread(void *new_connection)
{
	int client_connection;

	pthread_mutex_lock(&lock);
	client_connection = *((int *)new_connection);

	char inputBuffer[1024] = { 0 };
	int readSocket;
	struct entry *item = NULL;

	readSocket = read(client_connection, &inputBuffer, sizeof(inputBuffer));

	if(readSocket == -1) {
		printf("error with read: %s\n", strerror(errno));
	}


	item = (struct entry *)malloc(sizeof(*item));

	if(item == NULL) {
		perror("failed to allocate memory");
		exit(EXIT_FAILURE);
	}

	item->clientReq = inputBuffer;
	item->my_connection = client_connection;

	TAILQ_INSERT_TAIL(&tail_head, item, entries);


	if(busy) {
		/*printf("thread already working\n");*/
	}

	/*otherwise, assign work*/
	busy = 1;
	pthread_cond_signal(&cond);

	pthread_mutex_unlock(&lock);
	sleep(5);

	int return_val = 0;
	pthread_exit(&return_val);


}

/*This function parses a detected PUT request, and checks for
 *  file name validity and  the PUT request's content size
 *   it then sends file name, client socket connection, and the size of
 *    the content to handlePut()*/

void parsePut(int strDividerSize, char **strArray, char *fileName, int new_conn)
{
	int isPut = 0;
	char const *putStr = "PUT";
	int contentSize;
	int checkExists = 0;
	struct stat fileBuffer;
	struct stat filePathBuffer;
	char storeDir[40] = "StoredFiles/";
	char *filePath;
	char const *createdResponse = "HTTP/1.1 201 Created\r\n";

	for(int i = 0; i < strDividerSize; i++) {
		if(isPut == 1) {
			fileName = strArray[i];

			if(validName(fileName, new_conn) == 0) {
				filePath = strcat(storeDir, fileName);

				if(stat(filePath, &fileBuffer) == -1) {
					checkExists = -1;
				}

				contentSize = checkContentLength(strArray);
				handlePut(fileName, new_conn, contentSize);

				if((stat(filePath, &filePathBuffer) == 0) && (checkExists == -1)) {
					send(new_conn, createdResponse, strlen(createdResponse), 0);
				}
			}

			isPut = 0;
		}

		int comparePut = strcmp(strArray[i], putStr);

		if(comparePut == 0) {
			isPut = 1;
		}
	}
}

/*This function parses a detected GET request, and checks for
 *  file name validity and then sends file name and client socket connection to
 *   handleGet()*/
void parseGet(int strDividerSize, char **strArray, char *fileName, int new_conn)
{
	int isGet = 0;
	char const *getStr = "GET";

	for(int i = 0; i < strDividerSize; i++) {
		if(isGet == 1) {
			fileName = strArray[i];
			handleGet(fileName, new_conn);

			isGet = 0;
		}

		int compareGet = strcmp(strArray[i], getStr);

		if(compareGet == 0) {
			isGet = 1;
		}
	}
}

/*handleGet() checks whether a file exists on a server, and
 * writes it to the client socket with a content-length header if it does*/
void handleGet(char *fileName, int new_conn)
{
	struct stat fileBuffer;
	int8_t line;
	int inputFile = 0;
	char storeLine;
	int contentSize;
	char charContentSize[10000];

	if(stat(fileName, &fileBuffer) == 0) {
		inputFile = open(fileName, O_RDONLY);

		if(inputFile == -1) {
			fprintf(stderr, "open failed: %s\n", strerror(errno));
			exit(1);
		}

		contentSize = fileBuffer.st_size;
		sprintf(charContentSize, "%d", contentSize);

		while((line = read(inputFile, &storeLine, 1)) > 0) {
			/*Uncomment for content length header of GET request*/
			/*if (firstLoop == 0)
			 *              *              * 			{
			 *                           *                           * 							write(new_conn, "Content-Length: ",
			 *                                        * strlen("Content-Length:
			 *                                                     *                                        * ")); write(new_conn, charContentSize,
			 *                                                                  * strlen(charContentSize)); write(new_conn, "\n",
			 *                                                                               *                                                     * 1); firstLoop = -1;
			 *                                                                                            *                                                                  *
			 *                                                                                                         * }*/

			write(new_conn, &storeLine, 1);
		}
	}

	close(inputFile);
}

/*handlePUT() checks stores a file in a client PUT request
 * in a server directory (or creates directory if not existing).
 * It also has different handling for files with content length = 0
 * (connection hangs) and content length > 0 (file is written up to contentSize
 *  bytes) */
void handlePut(char *fileName, int new_conn, int contentSize)
{
	char socketBuffer;
	int inputFile = 0;
	int line;
	ssize_t writeSuccess = 0;
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	struct stat dirBuffer {
	};
	char storeDir[40] = "StoredFiles/";
	char *filePath;

	/*don't really need the below anymore*/
	if(fileName[0] == '/') {
		memmove(fileName, fileName + 1, strlen(fileName));
	}

	/*create path to where file will be stored*/
	filePath = strcat(storeDir, fileName);

	if(stat("StoredFiles", &dirBuffer) == -1) {
		if(mkdir("StoredFiles", 0755) != -1) {
			inputFile = open(filePath, O_WRONLY | O_CREAT, mode);

			if(inputFile == -1) {
				fprintf(stderr, "open failed: %s\n", strerror(errno));
				exit(1);
			}
		}
	} else {
		inputFile = open(filePath, O_WRONLY | O_CREAT, mode);

		if(inputFile == -1) {
			fprintf(stderr, "open failed: %s\n", strerror(errno));
			exit(1);
		}
	}

	/*goes here if content size of PUT is given as 0*/
	if(contentSize == 0) {
		while((line = read(new_conn, &socketBuffer, 1)) > 0) {
			write(inputFile, &socketBuffer, 1);

			if(writeSuccess != -1) {
				continue;
			}
		}

		/*otherwise, loops until contentSize number of bytes*/
	} else {
		int count = 0;
		while(count < contentSize) {
			if(read(new_conn, &socketBuffer, 1) > 0) {
				write(inputFile, &socketBuffer, 1);
				count++;
			} else {
				break;
			}
		}
	}

	close(inputFile);
}

/*This function parses strArray for the size of the
 *  PUT request content*/
int checkContentLength(char **strArray)
{
	int strArraySize = sizeof(strArray);
	int hasContent = 0;
	char const *contentLength = "Content-Length:";
	char *contentSize;
	int size = 0;

	for(int i = 0; i < strArraySize; i++) {
		if(hasContent == 1) {
			contentSize = strArray[i];
			hasContent = 0;
		}

		if(strstr(strArray[i], contentLength)) {
			hasContent = 1;
		}
	}

	size = atoi(contentSize);
	return size;
}

/*This function checks whether the given file name from the client is
 *  valid- it is 27 characters and chosen from the allowed 64 characters*/
int validName(char *fileName, int new_conn)
{
	char newChar;
	int numCode;
	int i = 0;
	int isValid = 0;
	char const *badRequest = "HTTP/1.1 400 Bad Request\r\n";

	if(strlen(fileName) != 27) {
		isValid = -1;
	}

	for(i = 0; fileName[i] != '\0'; ++i) {
		newChar = fileName[i];
		numCode = (int)newChar;
		if(!(numCode >= 48 && numCode <= 57) && !(numCode >= 65 && numCode <= 90)
				&& !(numCode >= 97 && numCode <= 122) && numCode != 95 && numCode != 45) {
			isValid = -1;
		}
	}

	if(isValid == -1) {
		send(new_conn, badRequest, strlen(badRequest), 0);
		return 1;
	}

	return 0;
}
