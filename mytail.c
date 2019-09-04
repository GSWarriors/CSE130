#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char *argv[]) {
        int num = 1;
        while(num < argc){
        int file = open(argv[num], O_RDONLY);

        char buffer[2048];
        char newline[2048];
        int bytes=0, i=0, count=1, a=0, total=0, d = 0, y =0, bytes2 =0, l = 0, count2 = 0, you = 0;

        if(argc <= 1){
                perror("File is NULL");
                return(-1);
        }
	if(file == -1){
		perror("Invalid file");
		return(-1);
	}
                char c;
                while(read(file, &c, 1) == 1){
                        buffer[i]= c;
                        bytes++;
                        if(buffer[i] == '\n'){
                                count++;
                                }
                                i++;        
                        }
                        for(d = 1; d <= count; d++){                    /* find number of lines before last 10 lines = d */
                                        if((count-d) == 10){
                                        y = d;
                                        break;
                                        }
                                }     
                        close(file);        
                        
        file = open(argv[num], O_RDONLY);
         char r;
        while(read(file, &r, 1) == 1){
                        newline[l]= r;
                        bytes2++;
                        if(newline[l] == '\n'){
                                count2++;
                                if(count2 == (y-1)){
                                        you = l;
                                }
                        }
                l++;        
        }

        if(count <=10){                                         /* if file has less then 10 lines, just output whole file */
                write(STDOUT_FILENO, buffer, i);
                write(1, "\n", 1);
                write(1, "\n", 1);
                close(file);
        
        }
        else{
                write(STDOUT_FILENO, &newline[you], l-you); /*writing from specific byte*/
                write(1, "\n", 1);
                write(1, "\n", 1);
                close(file);
                
        }

        num++;
        }

}
