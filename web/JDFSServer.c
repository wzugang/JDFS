/**
    JDFSServer: http server component of JDFS
    Copyright (C) 2017  zhang jun
    contact me: zhangjunhust@hust.edu.cn
    		http://www.cnblogs.com/junhuster/
    		http://weibo.com/junhuster 

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 **/
#include "../header/JDFSServer.h"
#include "../header/network.h"
#define debug 1
unsigned char server_buffer[1024*1025];
int Http_server_bind_and_listen(char *ip, int port, int *server_listen_fd){

	if(ip==NULL || port<0 || server_listen_fd==NULL){
		printf("Http_server_bind_and_listen: argument error\n");
		exit(0);
	}

	*server_listen_fd=socket(AF_INET,SOCK_STREAM,0);
	if((*server_listen_fd)<0){
		perror("Http_server_bind_and_listen, socket");
		exit(0);
	}

	struct sockaddr_in server_socket_address;
	server_socket_address.sin_family=AF_INET;
	server_socket_address.sin_port=htons(port);
	int ret=inet_pton(AF_INET,ip,&(server_socket_address.sin_addr.s_addr));
	if(ret!=1){
		printf("Http_server_bind_and_listen: inet_pton failed\n");
		exit(0);
	}

    int ret1=bind(*server_listen_fd,(struct sockaddr *)&server_socket_address,sizeof(server_socket_address));
    if(ret1==-1){
    	perror("Http_server_bind_and_listen, bind");
    	exit(0);
    }

    int ret2=listen(*server_listen_fd,server_listen_queue);
    if(ret2==-1){
    	perror("Http_server_bind_and_listen,listen");
    	exit(0);
    }

    return 0;
}

int Http_server_body(char *ip, int port, int *server_listen_fd, threadpool *pool){

	if(ip==NULL || port<0 || server_listen_fd==NULL){
		printf("Http_server_body: argument error\n");
		exit(0);
	}

	Http_server_bind_and_listen(ip, port, server_listen_fd);

	struct epoll_event event_for_epoll_ctl;
	struct epoll_event event_for_epoll_wait[event_for_epoll_wait_num];

	event_for_epoll_ctl.data.fd=*server_listen_fd;
	event_for_epoll_ctl.events= EPOLLIN;

	int epoll_fd=epoll_create(20);
	if(epoll_fd==-1){
		perror("Http_server_body,epoll_create");
		exit(0);
	}

	int ret=epoll_ctl(epoll_fd,EPOLL_CTL_ADD,*server_listen_fd,&event_for_epoll_ctl);
	if(ret==-1){
		perror("Http_server_body,epoll_ctl");
		exit(0);
	}

	int num_of_events_to_happen=0;
	while(1){

		num_of_events_to_happen=epoll_wait(epoll_fd,event_for_epoll_wait,event_for_epoll_wait_num,-1);
		if(num_of_events_to_happen==-1){
			perror("Http_server_body,epoll_wait");
			exit(0);
		}

		for(int i=0;i<num_of_events_to_happen;i++){
			struct sockaddr_in client_socket;
			int client_socket_len=sizeof(client_socket);
			if(*server_listen_fd==event_for_epoll_wait[i].data.fd){
				int client_socket_fd=accept(*server_listen_fd,(struct sockaddr *)&client_socket,&client_socket_len);
				if(client_socket_fd==-1){
					continue;
				}

				event_for_epoll_ctl.data.fd=client_socket_fd;
				event_for_epoll_ctl.events=EPOLLIN;

				epoll_ctl(epoll_fd,EPOLL_CTL_ADD,client_socket_fd,&event_for_epoll_ctl);

			}else if(event_for_epoll_wait[i].events & EPOLLIN){
				int client_socket_fd=event_for_epoll_wait[i].data.fd;
				if(client_socket_fd<0){
					continue;
				}

				memset(server_buffer,0,sizeof(server_buffer));
				int ret=recv(client_socket_fd,server_buffer,sizeof(http_request_buffer)+4+upload_one_piece_size,0);
				if(ret<=0){
					close(client_socket_fd);
					continue;
				}

				http_request_buffer *hrb=(http_request_buffer *)server_buffer;
				
				if(hrb->request_kind==0){

					callback_arg_query *cb_arg_query=(callback_arg_query *)malloc(sizeof(callback_arg_query));
					cb_arg_query->socket_fd=client_socket_fd;
					strcpy(cb_arg_query->file_name, hrb->file_name);
					threadpool_add_jobs_to_taskqueue(pool, Http_server_callback_query, (void *)cb_arg_query,0);

				}else if(hrb->request_kind==1){
					printf("accept %s from client, range(byte): %ld---%ld\n",hrb->file_name,hrb->num1,hrb->num2);
					FILE *fp=NULL;
					char *file_name=hrb->file_name;
					if(hrb->num1==0){
						fp=fopen(file_name, "w+");
					}else{
						fp=fopen(file_name,"r+");
					}


					if(fp==NULL){
						perror("Http_server_body,fopen");
						close(client_socket_fd);
					}else{

						long offset=hrb->num1;
						fseek(fp, offset, SEEK_SET);

						int ret=fwrite(server_buffer+sizeof(http_request_buffer)+4, hrb->num2-hrb->num1+1, 1, fp);
						if(ret!=1){
							close(client_socket_fd);
						}
					}

					fclose(fp);

				}else if(hrb->request_kind==2){
            		long range_begin=hrb->num1;
            		long range_end=hrb->num2;
            		#ifdef debug
            		printf("transfer %s to client, range(byte): %ld -- %ld\n",hrb->file_name,range_begin,range_end);
            		#endif

            		callback_arg_download *cb_arg_download=(callback_arg_download *)malloc(sizeof(callback_arg_download));
            		cb_arg_download->socket_fd=client_socket_fd;
            		cb_arg_download->range_begin=range_begin;
            		cb_arg_download->range_end=range_end;

            		strcpy(cb_arg_download->file_name, hrb->file_name);

            		threadpool_add_jobs_to_taskqueue(pool, Http_server_callback_download, (void*)cb_arg_download,2);

				}else{

				}

				
				//epoll delete client_fd

			}
		}

	}

	return 0;

}

void *Http_server_callback_query(void *arg){

	callback_arg_query *cb_arg_query=(callback_arg_query *)arg;
	char *filename=cb_arg_query->file_name;
	int client_socket_fd=cb_arg_query->socket_fd;
	
	FILE *fp=fopen(filename,"r");
    if(NULL==fp) {
	    printf("file:%s not exists, please provide the right name\n",filename);
	    close(client_socket_fd);
        return (void *)1;
    }

    fseek(fp, 0, SEEK_END);
    long file_size=ftell(fp);

    http_request_buffer hrb;
    hrb.num1=file_size;
    send(client_socket_fd,&hrb,sizeof(http_request_buffer),0);
    if(fclose(fp)!=0){
    	perror("Http_server_callback_query, fclose\n");
    	return (void *)2;
    }

    return NULL;
}

void *Http_server_callback_upload(void *arg){


}

void *Http_server_callback_download(void *arg){

	callback_arg_download *cb_arg_download=(callback_arg_download *)arg;
	char *file_name=cb_arg_download->file_name;
	int  client_socket_fd=cb_arg_download->socket_fd;
	long range_begin=cb_arg_download->range_begin;
	long range_end=cb_arg_download->range_end;
	unsigned char *server_buffer=cb_arg_download->server_buffer;
	FILE *fp=fopen(file_name, "r");
	if(fp==NULL){
		perror("Http_server_callback_download,fopen\n");
		close(client_socket_fd);
		return (void *)1;
	}


	http_request_buffer *hrb=(http_request_buffer *)server_buffer;
	hrb->num1=range_begin;
	hrb->num2=range_end;

	fseek(fp, range_begin, SEEK_SET);
    long http_request_buffer_len=sizeof(http_request_buffer);
    memcpy(server_buffer+http_request_buffer_len,"JDFS",4);

    fread(server_buffer+http_request_buffer_len+4, range_end-range_begin+1, 1, fp);
    int send_num=0;
    int ret=send(client_socket_fd,server_buffer+send_num,http_request_buffer_len+4+range_end-range_begin+1-send_num,0); 
    if(ret==-1){
        perror("Http_server_body,send");
        close(client_socket_fd);
    }   

    if(fclose(fp)!=0){
    	perror("Http_server_callback_download,fclose");
    }        		
}

int main(int argc, char const *argv[])
{
	char *ip="192.168.137.132";
	int port=8888;
	int server_listen_fd=0;
	threadpool *pool=(threadpool *)malloc(sizeof(threadpool));
	threadpool_create(pool, 6, 20);
	Http_server_body(ip,port,&server_listen_fd,pool);
	destory_threadpool(pool);
	return 0;
}