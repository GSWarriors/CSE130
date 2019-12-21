#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
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

void parsePut(int strDividerSize, char **strArray, char *fileName, int new_conn, char *logFile);
void parseGet(int strDividerSize, char **strArray, char *fileName, int new_conn, char *logFile);
void handleGet(char *fileName, int new_conn, char *logFile);
void handlePut(char *fileName, int new_conn, int contentSize, char *logFile);
int checkContentLength(char **strArray);
int validName(char *fileName, int new_conn);
void *assignThread(void *new_connection);
void *handleClients(void *);
void parseRequest(int new_connection, char *inputBuffer, int bufferSize, char *logFile);
void writeLog(char *filePath, char *putStr, int contentSize, char *logFile, int requestNum);
void writeCache(char *fileName, int contentSize, int new_conn);
void updateContents(char *fileName, int contentSize, int new_conn);

int offset = 0;
int fileSize = 0;
int cacheOn = 0;

struct myCache {
	char *fileContents;
	char *nameofFile;

	TAILQ_ENTRY(myCache) entries;
};

/*tail queue requires a head*/
TAILQ_HEAD(, myCache) tail_head;

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
	char *logFile = NULL;
	int readSocket;
	char *cacheFlag = NULL;

	/*command line inputs, for only IP address, and for IP address and PORT*/

	/**Use these args for single threaded logging**/
	if(argc == 2) {
		myAddress = argv[1];
		PORT = 80;
	} else if(argc == 3) {
		myAddress = argv[1];
		PORT = atoi(argv[2]);
	} else if(argc == 4) {
		cacheFlag = argv[1];
		myAddress = argv[2];
		PORT = atoi(argv[3]);
	}

	/*else if(argc == 4) {
	 * 	 *      * 	 * 		logFile = argv[2];
	 * 	 	 *           * 	 	 * 				myAddress = argv[3];
	 * 	 	 	 *                * 	 	 	 * 						PORT = 80;
	 * 	 	 	 	 *                     * 	 	 	 	 * 							} else if(argc == 5) {
	 * 	 	 	 	 	 *                          * 	 	 	 	 	 * 									logFile = argv[2];
	 * 	 	 	 	 	 	 *                               * 	 	 	 	 	 	 * 											myAddress = argv[3];
	 * 	 	 	 	 	 	 	 *                                    * 	 	 	 	 	 	 	 * 													PORT =
	 * 	 	 	 	 	 	 	 	 *                                         * atoi(argv[4]);
	 * 	 	 	 	 	 	 	 	 	 *                                              * 	 	 	 	 	 	 	 	 * 														}*/

	if(cacheFlag != NULL && strcmp(cacheFlag, "-c") == 0) {
		cacheOn = 1;
	}

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

	/*initialize tail queue*/
	TAILQ_INIT(&tail_head);

	while(1) {
		int new_connection =
			accept(serverSocket, (struct sockaddr *)currentAddress, (socklen_t *)&addressLen);

		if(new_connection < 0) {
			perror("socket not accepted");
			return -1;
		}

		char inputBuffer[1024] = { 0 };
		readSocket = read(new_connection, &inputBuffer, sizeof(inputBuffer));

		/*if (strlen(logFile) != 1 && strlen(logFile) > 0) {*/
		parseRequest(new_connection, inputBuffer, sizeof(inputBuffer), logFile);
		/*}*/

		/*wait to close the connection (want it to process)*/
		close(new_connection);
	}

	return 0;
}

void parseRequest(int new_conn, char *inputBuffer, int bufferSize, char *logFile)
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

	char const *putStr = "PUT";
	char const *getStr = "GET";
	char *fileName = NULL;
	int putRequest = 0;
	int getRequest = 0;

	if(strcmp(strArray[0], "GET") == 0) {
		strDividerSize -= 2;
	}
	/*this line needed to differentiate between PUT and GET requests*/

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
		parsePut(strDividerSize, strArray, fileName, new_conn, logFile);
	} else if(getRequest == 1) {
		parseGet(strDividerSize, strArray, fileName, new_conn, logFile);
	}

	/*writes to STDOUT whatever is in the input buffer*/
	write(STDOUT_FILENO, inputBuffer, bufferSize);
}

/*This function parses a detected PUT request, and checks for
 *  *  *  *  *  *   file name validity and  the PUT request's content size
 *   *   *   *   *   *   it then sends file name, client socket connection, and the size of the
 *    *    *    *    *    *    content to handlePut()*/

void parsePut(int strDividerSize, char **strArray, char *fileName, int new_conn, char *logFile)
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
				handlePut(fileName, new_conn, contentSize, logFile);

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
 *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  * file name validity and then sends file name and client
 *   *   * socket connection to
 *    *    *   *   *   *   *   *   *   * handleGet()*/
void parseGet(int strDividerSize, char **strArray, char *fileName, int new_conn, char *logFile)
{
	int isGet = 0;
	char const *getStr = "GET";

	for(int i = 0; i < strDividerSize; i++) {
		if(isGet == 1) {
			fileName = strArray[i];
			handleGet(fileName, new_conn, logFile);

			isGet = 0;
		}

		int compareGet = strcmp(strArray[i], getStr);

		if(compareGet == 0) {
			isGet = 1;
		}
	}
}

/*handleGet() checks whether a file exists on a server, and
 *  * writes it to the client socket with a content-length header if
 *   * it does. In the cache implementation, it receives its contents from the 
 *    * cache and not local*/
void handleGet(char *fileName, int new_conn, char *logFile)
{
	struct stat fileBuffer;
	int8_t line;
	int inputFile = 0;
	char storeLine;
	int contentSize = 0;
	char charContentSize[10000];
	int inCache = 0; 

	char getStr[150] = "GET ";
	char lengthStr[9] = " length ";
	char *getRequest = NULL;
	int firstLoop = 0;


	int count = 0;
	char *newfileName;

	/*make StoredFiles/filename to just filename*/
	newfileName = (char *)malloc(strlen(fileName) + 1 * sizeof(char));

	for (int i = 0; i < strlen(fileName); i++) {
		if (count == 12) {
			strcpy(newfileName, &fileName[i]);
		}
		count++;
	}


	if (stat(fileName, &fileBuffer) == 0) {
		if(!TAILQ_EMPTY(&tail_head)) {
			struct myCache *checkFile;
			TAILQ_FOREACH(checkFile, &tail_head, entries) {
				if(strcmp(checkFile->nameofFile, newfileName) == 0) {
					inCache = 1;
				}
			}
		}

	}



	if(inCache == 1 && cacheOn) {
		printf("in cache GET\n");

		contentSize = fileBuffer.st_size;

		sprintf(charContentSize, "%d", contentSize);
		getRequest = strcat(getStr, fileName);
		getRequest = strcat(getStr, lengthStr);
		getRequest = strcat(getStr, charContentSize);

		if(!TAILQ_EMPTY(&tail_head)) {
			struct myCache *checkFile;
			TAILQ_FOREACH(checkFile, &tail_head, entries)
			{
				if(strcmp(checkFile->nameofFile, newfileName) == 0) {
					printf("size of file contents %d\n", strlen(checkFile->fileContents));
					printf("the contents\n");

					char *ptr = checkFile->fileContents;
					write(new_conn, &ptr[0], strlen(checkFile->fileContents));

					/*if (firstLoop == 0) {
					 * 					 *                      * 					 * 							write(new_conn, "Content-Length: ", strlen("Content-Length:
					 * 					 					 *                                           * "));
					 * 					 					 					 *                                                                * 					 					 * 														write(new_conn, charContentSize,
					 * 					 					 					 					 *                                                                                     * strlen(charContentSize));
					 * 					 					 					 					 					 *                                                                                                          * 					 					 					 * 																					write(new_conn, "\n",
					 * 					 					 					 					 					 					 *                                                                                                                               * 1);
					 * 					 					 					 					 					 					 					 *                                                                                                                                                    * 					 					 					 					 * 																												firstLoop =
					 * 					 					 					 					 					 					 					 					 *                                                                                                                                                                         * -1;
					 * 					 					 					 					 					 					 					 					 					 *                                                                                                                                                                                              * 					 					 					 					 					 *
					 * 					 					 					 					 					 					 					 					 					 					 *                                                                                                                                                                                                                   * }
					 * 					 					 					 					 					 					 					 					 					 					 					 *                                                                                                                                                                                                                                        * 					 					 					 					 					 					 *
					 * 					 					 					 					 					 					 					 					 					 					 					 					 *                                                                                                                                                                                                                                                             * 					 					 					 					 					 					 					 *
					 * 					 					 					 					 					 					 					 					 					 					 					 					 					 *                                                                                                                                                                                                                                                                                  * }*/
			}
		}
	}

} else if(stat(fileName, &fileBuffer) == 0 && inCache == 0) {
	printf("in regular GET\n");

	inputFile = open(fileName, O_RDONLY);

	if(inputFile == -1) {
		fprintf(stderr, "open failed: %s\n", strerror(errno));
		exit(1);
	}

	contentSize = fileBuffer.st_size;

	sprintf(charContentSize, "%d", contentSize);
	getRequest = strcat(getStr, fileName);
	getRequest = strcat(getStr, lengthStr);
	getRequest = strcat(getStr, charContentSize);

	while((line = read(inputFile, &storeLine, 1)) > 0) {
		if(firstLoop == 0) {
			write(new_conn, "Content-Length: ", strlen("Content-Length: "));
			write(new_conn, charContentSize, strlen(charContentSize));
			write(new_conn, "\n", 1);
			firstLoop = -1;
		}

		write(new_conn, &storeLine, 1);
	}
}

close(inputFile);
/*writeLog(fileName, getRequest, contentSize, logFile, 0);*/
}

/*handlePUT() checks stores a file in a client PUT request
 *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  * in a server directory (or creates directory if not
 *   *   * existing).
 *    *    *   *   *   *   *   *   *   *   *   *   *   *   *   * It also has different handling for files with
 *     *     * content length = 0
 *      *      *    *    *    *    *    *    *    * (connection hangs)
 *       *       *     *     *     *     *     *     *     *    *    *    *    *    *    * and content length > 0
 *        *        * (file is written up to contentSize bytes)
 *         *         *      *      *      *      *      *      *      *     *     *     *     *     *     * */
void handlePut(char *fileName, int new_conn, int contentSize, char *logFile)
{
	/*sizeStr 50 to account for very large files length*/
	/*putStr length 150 to account for other concatenated strings*/

	char socketBuffer;
	int inputFile = 0;
	int line;
	ssize_t writeSuccess = 0;
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	struct stat dirBuffer {
	};
	struct stat filePathBuffer;
	char storeDir[40] = "StoredFiles/";
	char *filePath;
	char putStr[150] = "PUT ";
	char lengthStr[9] = " length ";
	char sizeStr[50];
	char *putRequest;

	/*concatenate the request line of a log together*/
	sprintf(sizeStr, "%d", contentSize);
	putRequest = strcat(putStr, fileName);
	putRequest = strcat(putStr, lengthStr);
	putRequest = strcat(putStr, sizeStr);

	/*don't really need the below anymore*/
	if(fileName[0] == '/') {
		memmove(fileName, fileName + 1, strlen(fileName));
	}

	/*create path to where file will be stored*/
	filePath = strcat(storeDir, fileName);

	/*check whether file path already exists*/
	if(stat(filePath, &filePathBuffer) == 0 && cacheOn) {
		updateContents(fileName, contentSize, new_conn);
	} else {
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

		if(cacheOn) {
			writeCache(fileName, contentSize, new_conn);
		}
	}

	/*writeLog(filePath, putRequest, contentSize, logFile, 1);*/
}

/*writes the new files contents to the a buffer, and then
 *  *  *  *  * adds it to the cache queue*/

/*go through TAILQ, check whether cache full (size 4). if it is, then remove head
 *  *  * 		 * 		 * 		 * 		and add tail (FIFO), otherwise, just add tail*/

/*remove from head*/

void writeCache(char *fileName, int contentSize, int new_conn)
{
	struct myCache *item = NULL;
	int openFile;
	int line;
	char storeDir[40] = "StoredFiles/";
	char removeStoreDir[40] = "StoredFiles/";
	char *filePath;
	char *removePath;
	char socketBuffer;

	item = (struct myCache *)malloc(sizeof(*item));

	if(item == NULL) {
		perror("failed to allocate memory");
		exit(EXIT_FAILURE);
	}

	/*assign name of file to filename, allocate space for size of the file to be written
	 * 	 *      * 	 * 	 * 	 * 	to fileContents*/

	printf("comes here!\n");

	filePath = strcat(storeDir, fileName);
	item->nameofFile = (char *)malloc(strlen(fileName) + 1 * sizeof(char));
	item->fileContents = (char *)malloc(contentSize + 1 * sizeof(char));

	openFile = open(filePath, O_RDONLY);

	if(openFile == -1) {
		fprintf(stderr, "open failed: %s\n", strerror(errno));
		exit(1);
	}

	if(fileSize < 4) {
		/*copy filename into item->nameofFile*/

		strcpy(item->nameofFile, fileName);

		/*printf("name of filse: %s\n", item->nameofFile);*/

		while((line = read(openFile, &socketBuffer, 1)) > 0) {
			strncat(item->fileContents, &socketBuffer, 1);
		}

		TAILQ_INSERT_TAIL(&tail_head, item, entries);
		fileSize++;

	} else if(fileSize >= 4) {
		struct myCache *first_item;
		first_item = TAILQ_FIRST(&tail_head);

		int inputFile;
		removePath = strcat(removeStoreDir, first_item->nameofFile);

		printf("the file to be removed: %s\n", first_item->nameofFile);

		inputFile = open(removePath, O_WRONLY);

		if(inputFile == -1) {
			fprintf(stderr, "open failed: %s\n", strerror(errno));
			exit(1);
		}

		printf("length of file stored: %d\n", strlen(first_item->fileContents));
		write(inputFile, &first_item->fileContents[0], strlen(first_item->fileContents));

		close(inputFile);

		TAILQ_REMOVE(&tail_head, first_item, entries);
		free(first_item);

		/*now, add to tail*/
		item->nameofFile = fileName;
		while((line = read(openFile, &socketBuffer, 1)) > 0) {
			strncat(item->fileContents, &socketBuffer, 1);
		}

		TAILQ_INSERT_TAIL(&tail_head, item, entries);
		fileSize++;
	}
	close(openFile);
}

/*updates the contents of a given file if it's found to be in the queue already*/
/*clears buffer given and reallocates memory to include new size of the file*/
void updateContents(char *fileName, int contentSize, int new_conn)
{
	char socketBuffer;
	int isUpdated = 0;

	if(!TAILQ_EMPTY(&tail_head)) {
		struct myCache *checkFile;
		TAILQ_FOREACH(checkFile, &tail_head, entries)
		{
			if(strcmp(checkFile->nameofFile, fileName) == 0) {
				memset(checkFile->fileContents, 0, sizeof(checkFile->fileContents));
				printf("reallocating, content size now: %d\n", contentSize);

				realloc(checkFile->fileContents, (contentSize + 1 * sizeof(char)));
				/*check for filename, add to buffer*/

				int count = 0;
				while(count < contentSize) {
					if(read(new_conn, &socketBuffer, 1) > 0) {
						strncat(checkFile->fileContents, &socketBuffer, 1);

						count++;
					} else {
						break;
					}
				}
				isUpdated = 1;
			}

			if(isUpdated == 1) {
				break;
			}
		}
	}
}

/*writes to log file depending on kind of request specified by requestType*/
void writeLog(char *filePath, char *request, int contentSize, char *logFile, int requestType)
{
	struct stat fileBuffer;
	int openInput = 0;
	int openLog = 0;
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

	openInput = open(filePath, O_RDONLY);

	if(stat(logFile, &fileBuffer) == -1) {
		openLog = open(logFile, O_WRONLY | O_CREAT, mode);

	} else {
		openLog = open(logFile, O_WRONLY);
	}

	if(openInput == -1) {
		fprintf(stderr, "openInput failed: %s\n", strerror(errno));
		exit(1);
	}

	if(openLog == -1) {
		fprintf(stderr, "openLog failed: %s\n", strerror(errno));
		exit(1);
	}

	if(requestType == 0) {
		pwrite(openLog, request, strlen(request), offset);
		offset += strlen(request);

	} else if(requestType == 1) {
		if(contentSize <= 20) {
			char line;
			char buffer;
			char hexStr[10];
			int count = 0;

			pwrite(openLog, request, strlen(request), offset);
			offset += strlen(request);

			pwrite(openLog, "\n", 1, offset);
			offset++;

			pwrite(openLog, "00000000 ", 9, offset);
			offset += 9;

			while((line = pread(openInput, &buffer, 1, count)) > 0) {
				char subHex[3];
				sprintf(hexStr + 1 * 2, "%02X", buffer);
				memcpy(subHex, &hexStr[2], 2);
				subHex[2] = '\0';

				pwrite(openLog, subHex, strlen(subHex), offset);
				offset += strlen(subHex);

				pwrite(openLog, " ", 1, offset);
				offset++;

				count += 1;
			}

		} else if(contentSize > 20) {
			char line;
			char buffer;
			int numZeroes;
			char offsetStr[8];
			int offsetSize;

			char hexStr[10];
			int count = 0;

			pwrite(openLog, request, strlen(request), offset);
			offset += strlen(request);

			pwrite(openLog, "\n", 1, offset);
			offset++;

			while((line = pread(openInput, &buffer, 1, count)) > 0) {
				if(count == 0) {
					pwrite(openLog, "00000000 ", 9, offset);
					offset += 9;

				} else if(count != 0 && count % 20 == 0) {
					pwrite(openLog, "\n", 1, offset);
					offset++;

					sprintf(offsetStr, "%d", count);
					/*printf("this is the offset string: %s\n", offsetStr);*/

					if(count != 0) {
						offsetSize = floor(log10(abs(offset))) + 1;
					}

					numZeroes = (sizeof(offsetStr) - offsetSize) + 1;

					char zeroesStr[numZeroes];
					int i = 0;

					for(i = 0; i < sizeof(zeroesStr); i++) {
						zeroesStr[i] = '0';
					}

					/*copy the zeroes to get the counter string for the current line*/
					char src[8], dest[8];
					strcpy(src, offsetStr);
					/*printf("src is %s\n", src);*/

					strcpy(dest, zeroesStr);
					strcat(dest, src);

					pwrite(openLog, dest, 8, offset);
					offset += 8;

					pwrite(openLog, " ", 1, offset);
					offset += 1;
				}

				char subHex[3];
				sprintf(hexStr + 1 * 2, "%02X", buffer);
				memcpy(subHex, &hexStr[2], 2);
				subHex[2] = '\0';

				pwrite(openLog, subHex, strlen(subHex), offset);
				offset += strlen(subHex);

				pwrite(openLog, " ", 1, offset);
				offset++;
				count += 1;
			}
		}
	}

	pwrite(openLog, "\n========\n", 10, offset);
	offset += 10;

	close(openInput);
	close(openLog);
}

/*This function parses strArray for the size of the
 *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  * PUT request content*/
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
 *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  * valid- it is 27 characters and chosen from the allowed 64
 *   *   * characters*/
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
