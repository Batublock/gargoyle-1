/*
 *  Copyright © 2008 by Eric Bishop <eric@gargoyle-router.com>
 * 
 *  This file is free software: you may copy, redistribute and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation, either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This file is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */



#include "http_minimal_client.h"

#ifdef USE_STRING_UTIL
#include "string_util.h"
#endif

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>





#define UNKNOWN_PROTO 1
#define HTTP_PROTO 2
#define HTTPS_PROTO 3

static const char cb64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


//general utility functions
#ifndef STRING_UTIL_H
char* dynamic_strcat(int num_strs, ...);  //multi string concatenation function (uses dynamic memory allocation)
void to_lowercase(char* str);
#endif

int char_index(char* str, int chr);
char* encode_base_64_str( char* original, int linesize );
void encode_block_base64( unsigned char in[3], unsigned char out[4], int len );
char* escape_chars_to_hex(char* str, char* chars_to_escape);
int tcp_connect(char* hostname, int port);


//http read/write functions
void* initialize_connection_http(char* host, int port);
int read_http(void* connection_data, char* read_buffer, int read_length);
int write_http(void* connection_data, char* data, int data_length);
void destroy_connection_http(void* connection_data);

//functions for actually performing http request
char* create_http_request(url_data* url);
http_response* get_http_response(void* connection_data, int (*read_connection)(void*, char*, int));
http_response* retrieve_http(	url_data *url, 
				void* (*initialize_connection)(char*, int), 
				int (*read_connection)(void*, char*, int),
				int (*write_connection)(void*, char*, int),
				void (*destroy_connection)(void*)
				);





// SSL definitions
#ifdef HAVE_SSL

	#ifdef USE_OPENSSL
		#include <openssl/ssl.h>
	#endif
	
	#ifdef USE_MATRIXSSL
		#include "matrixssl_helper.h"
		typedef sslKeys_t SSL_CTX;
		void SSL_CTX_free(SSL_CTX* ctx){ free(ctx); }
	#endif

	typedef struct
	{
		int socket;
		SSL* ssl;
		SSL_CTX* ctx;
	} ssl_connection_data;

	void* initialize_connection_https(char* host, int port);
	int read_https(void* connection_data, char* read_buffer, int read_length);
	int write_https(void* connection_data, char* data, int data_length);
	void destroy_connection_https(void* connection_data);

#endif
// end SSL definitions



/**********************************************
 * Externally available function definitions
 * ********************************************/

http_response* get_url_str(char* url_str)
{
	url_data* url = parse_url(url_str);
	http_response *reply = get_url(url);
	free_url(url);
	return reply;
}

http_response* get_url(url_data* url)
{
	http_response *reply = NULL;
	if(url->protocol == HTTP_PROTO)
	{
		reply = retrieve_http(url, initialize_connection_http, read_http, write_http, destroy_connection_http);
	}
	#ifdef HAVE_SSL
		if(url->protocol == HTTPS_PROTO)
		{
			reply = retrieve_http(url, initialize_connection_https, read_https, write_https, destroy_connection_https);
		}
	#endif

	return reply;
}

void free_http_response(http_response* page)
{
	if(page != NULL)
	{
		if(page->data != NULL)
		{
			free(page->data);
		}
		if(page->header != NULL)
		{
			free(page->header);
		}
		free(page);
	}
}



url_data* parse_url(char* url)
{
	
	url_data *new_url = (url_data*)malloc(sizeof(url_data));
	new_url->protocol = UNKNOWN_PROTO;
	new_url->user = NULL;
	new_url->password = NULL;
	new_url->hostname = NULL;
	new_url->port = -1;
	new_url->path = NULL;

	if(url == NULL)
	{
		return new_url;
	}


	

	
	//step 1, parse out protocol
	char* lower_url = strdup(url);
	to_lowercase(lower_url);

	char* remainder = NULL;
	if(strstr(lower_url, "http://") == lower_url)
	{
		new_url->protocol = HTTP_PROTO;
		new_url->port = 80;
		remainder = url+7;
	}
	else if(strstr(lower_url, "https://") == lower_url)
	{
		new_url->protocol = HTTPS_PROTO;
		new_url->port = 443;
		remainder = url+8;
	}
	else if(strstr(lower_url, "://") == NULL) //if no prefix provided assume HTTP 
	{
		new_url->protocol = HTTP_PROTO;
		new_url->port = 80;
		remainder = url;
	}
	free(lower_url);


	if(remainder != NULL) //if protocol is defined as something we don't support (e.g. ftp) do not parse url and return NULL
	{
		//step 2, parse out user/password if present
		int path_begin = char_index(remainder, '/');
		path_begin = path_begin >= 0 ? path_begin : strlen(remainder);

		int user_pass_end = char_index(remainder, '@');
		if(user_pass_end >= 0 && user_pass_end < path_begin)
		{
			//found user/password
			int user_end = char_index(remainder, ':');	
			user_end = user_end < user_pass_end && user_end >= 0 ? user_end : user_pass_end;
			new_url->user = (char*)malloc((user_end+1)*sizeof(char));
			memcpy(new_url->user, remainder, user_end);
			(new_url->user)[user_end] = '\0';
			if(user_end != user_pass_end)
			{
				int pass_length = user_pass_end-user_end-1;
				new_url->password = (char*)malloc((pass_length+1)*sizeof(char));
				memcpy(new_url->password, remainder+user_end+1, pass_length);
				(new_url->password)[pass_length] = '\0';
			}
			remainder = remainder + user_pass_end + 1;
			path_begin = char_index(remainder, '/');
			path_begin = path_begin >= 0 ? path_begin : strlen(remainder);
		}
		
		//step 3, parse out hostname & port, we escape characters after this point since
		//this will be included directly in GET request
		//this dynamicaly allocates memory , remember to free url at end
		char escape_chars[] = "\n\r\t\"\\ []<>{}|^~`:,";
		url = escape_chars_to_hex(remainder, escape_chars);
		remainder = url;
		
		int port_begin = char_index(remainder, ':');
		if(port_begin >= 0 && port_begin < path_begin)
		{
			new_url->hostname = (char*)malloc((port_begin+1)*sizeof(char));
			memcpy(new_url->hostname, remainder, port_begin);
			(new_url->hostname)[port_begin] = '\0';
			
			if(path_begin-port_begin-1 <= 5)
			{
				char port_str[6];
				memcpy(port_str, remainder+port_begin+1, path_begin-port_begin-1);
				port_str[ path_begin-port_begin-1] = '\0';
				
				int read;
				if(sscanf(port_str, "%d", &read) > 0)
				{
					if(read <= 65535)
					{
						new_url->port = read;
					}
				}
			}
		}
		else
		{
			new_url->hostname = (char*)malloc((path_begin+1)*sizeof(char));
			memcpy(new_url->hostname, remainder, path_begin);
			(new_url->hostname)[path_begin] = '\0';
		}
		remainder = remainder + path_begin;
		
		if(remainder[0] != '/')
		{
			new_url->path = (char*)malloc(2*sizeof(char));
			(new_url->path)[0] = '/';
			(new_url->path)[0] = '/';
		}
		else
		{
			//be a little more agressive in escaping path string
			new_url->path = strdup(remainder);
		}
		free(url); // free memory allocated from escaping characters
	}
	return new_url;
}


void free_url(url_data* url)
{
	if(url != NULL)
	{
		if(url->user != NULL)
		{
			free(url->user);
		}
		if(url->password != NULL)
		{
			free(url->password);
		}
		if(url->hostname != NULL)
		{
			free(url->hostname);
		}
		if(url->path != NULL)
		{
			free(url->path);
		}
		free(url);
	}
}



/**********************************************
 * Internal function definitions
 * ********************************************/






http_response* retrieve_http(	url_data *url, 
				void* (*initialize_connection)(char*, int), 
				int (*read_connection)(void*, char*, int),
				int (*write_connection)(void*, char*, int),
				void (*destroy_connection)(void*)
				)
{
	http_response *reply = NULL;

	if(url->hostname != NULL && url->port >= 0 && url->path != NULL)
	{
		
		void* connection_data = initialize_connection(url->hostname, url->port);
		if(connection_data != NULL)
		{
			char* request = create_http_request(url);
			//printf("request:\n%s", request);
			
			//send request
			int test = write_connection(connection_data, request, strlen(request));
			if(test == strlen(request))
			{
				reply = get_http_response(connection_data, read_connection);
			}
			free(request);
			destroy_connection(connection_data);
		}
	}
	return reply;
}

char* create_http_request(url_data* url)
{
	char *req_str1 = dynamic_strcat(	8,
					"GET ", 
					url->path, 
					" HTTP/1.0\r\n", 
					"User-Agent: http_minimal_client 1.0\r\n", 
					"Accept: */*\r\n", 
					"Connection: close\r\n", 
					"Host: ", 
					url->hostname
					);
	
	char port_str[8];
	if( (url->protocol == HTTP_PROTO && url->port != 80) || (url->protocol == HTTPS_PROTO && url->port != 443) )
	{
		sprintf(port_str, ":%d\r\n", url->port);
	}
	else
	{
		sprintf(port_str, "\r\n");
	}
	char* req_str2 = dynamic_strcat(2, req_str1, port_str);
	free(req_str1);

		
	if(url->user != NULL)
	{
		char* plain_auth = NULL;
		if(url->password == NULL)
		{
			plain_auth = strdup(url->user);
		}
		else
		{
			plain_auth = dynamic_strcat(3, url->user, ":", url->password);
		}
		char* encoded_auth = encode_base_64_str(plain_auth, 999999);
		

		req_str1 = dynamic_strcat(4, req_str2, "Authorization: Basic ", encoded_auth, "\r\n");
		free(req_str2);
		req_str2 = req_str1;

		free(plain_auth);
		free(encoded_auth);
	}
	req_str1 = dynamic_strcat(2, req_str2, "\r\n");
	free(req_str2);

	return req_str1;
}

http_response* get_http_response(void* connection_data, int (*read_connection)(void*, char*, int))
{
	http_response *reply = (http_response*)malloc(sizeof(http_response));
	reply->header = NULL;
	reply->data = NULL;
	reply->is_text = 0;
	reply->length = 0;

	char* http_data = NULL;
	int read_buffer_size = 1024;
	char* read_buffer = (char*)malloc(read_buffer_size*sizeof(char));
	int total_bytes_read = 0;
	int bytes_read;


	bytes_read = read_connection(connection_data, read_buffer, read_buffer_size);
	while(bytes_read > 0)
	{
		int updated_header = 0;
		if(reply->header == NULL)
		{
			int header_end = -1;
			char* cr_end = strstr(read_buffer, "\n\r\n");
			char* lf_end = strstr(read_buffer, "\n\n");
			if(cr_end != NULL && lf_end != NULL)
			{
				char *first_end = cr_end < lf_end ? cr_end : lf_end;
				int modifier = cr_end < lf_end ? 2 : 1;
				header_end = modifier+ (int)(first_end - read_buffer);
			}
			else if(cr_end != NULL)
			{
				header_end = 2+ (int)(cr_end - read_buffer);
			}
			else if(lf_end != NULL)
			{
				header_end = 1 + (int)(lf_end - read_buffer);
			}
			if(header_end < 0 && bytes_read < read_buffer_size)
			{
				header_end = bytes_read-1;
			}
			if(header_end > 0)
			{
				updated_header = 1;
			
				char* header = (char*)malloc((total_bytes_read+header_end+1)*sizeof(char));
				if(total_bytes_read > 0)
				{
					memcpy(header, http_data, total_bytes_read);
				}
				memcpy(header, read_buffer, header_end);
				header[total_bytes_read+header_end] = '\0';
				reply->header= strdup(header);

				free(http_data);
				total_bytes_read = (bytes_read-(header_end+1)) >= 0 ? (bytes_read-(header_end+1)) : 0;
				http_data = (char*)malloc( (total_bytes_read+1)*sizeof(char));
				memcpy(http_data, read_buffer+header_end+1, total_bytes_read );
				
				to_lowercase(header);
				char *content_start = strstr(header, "content-type:");
				if(content_start != NULL)
				{
					int content_end = char_index(content_start, '\n');
					char *content = (char*)malloc((content_end+1)*sizeof(char));
					memcpy(content, content_start, content_end);
					content[content_end] = '\0';
					reply->is_text = strstr(content, "text") == NULL ? 0 : 1;
					free(content);
				}
				free(header);
			}
		}
		if(updated_header ==0)
		{
			char* old_http_data = http_data;
			http_data = (char*)malloc((total_bytes_read + bytes_read+1)*sizeof(char));
			memcpy(http_data, old_http_data, total_bytes_read);
			memcpy(http_data+total_bytes_read, read_buffer, bytes_read);
			free(old_http_data);
			
			total_bytes_read = total_bytes_read + bytes_read;
		}
		bytes_read=read_connection(connection_data, read_buffer, read_buffer_size);
	}
	http_data[total_bytes_read] = '\0';
	reply->length = total_bytes_read;
	reply->data = http_data;

	free(read_buffer);
	return reply;
}




int char_index(char* str, int ch)
{
	char* result = strchr(str, ch);
	int return_value = result == NULL ? -1 : (int)(result - str);
	return return_value;
}


int tcp_connect(char* hostname, int port)
{
	struct hostent* host;
       	host = gethostbyname(hostname);
	if(host == NULL)
	{
		return -1;
	}

	int sockfd;
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0)
	{
		return -1;
	}
	

	struct sockaddr_in address;
	address.sin_family= AF_INET;
	address.sin_addr.s_addr = ((struct in_addr *)host->h_addr)->s_addr;
	address.sin_port = htons(port); //htons is necessary -- it makes sure byte ordering is correct

	int address_len;
	address_len = sizeof(address);

	int connection;
	connection = connect(sockfd, (struct sockaddr *)&address, address_len);
	if(connection < 0)
	{
		return -1;
	}
	
	return sockfd;
}


char* escape_chars_to_hex(char* str, char* chars_to_escape)
{
	
	char* new_str = NULL;
	if(str != NULL)
	{
		if(chars_to_escape == NULL)
		{
			new_str = strdup(str);
		}
		else
		{
			new_str = strdup("");
			int last_piece_start = 0;
			int str_index;
			for(str_index = 0; str[str_index] != '\0'; str_index++)
			{
				int found = 0;
				int escape_index;
				for(escape_index = 0; chars_to_escape[escape_index] != '\0' && found == 0; escape_index++)
				{
					found = chars_to_escape[escape_index] == str[str_index] ? 1 : 0;
				}
				if(found == 1)
				{
					int last_piece_length = str_index - last_piece_start;
					char* last_piece = (char*)malloc((1+last_piece_length)*sizeof(char));
					memcpy(last_piece, str+last_piece_start, last_piece_length);
					last_piece[last_piece_length] = '\0';
						
					char buf[5];
					sprintf(buf, "%%%X", str[str_index]);

					char* old_str = new_str;
					new_str = dynamic_strcat(3, old_str, last_piece, buf);
					free(old_str);
					last_piece_start = str_index+1;
				}	
			}
			if(last_piece_start < str_index)
			{
				char* old_str = new_str;
				new_str = dynamic_strcat(2, old_str, str+last_piece_start);
				free(old_str);
			}
		}	
	}	
	return new_str;
}


char* encode_base_64_str( char* original, int linesize )
{
	unsigned char in[3];
	unsigned char out[4];
	int i, len, blocksout = 0;

	
	char* encoded;
	int original_index = 0; 
	int encoded_index = 0;

	if(original == NULL)
	{
		encoded = (char*)malloc(sizeof(char));
		encoded[0] = '\0';
	}
	else
	{
		encoded = (char*)malloc( ((4*strlen(original)/3)+4)*sizeof(char) );
		if(original[original_index] == '\0')
		{
			encoded[0] = '\0';
		}
		while( original[original_index] != '\0')
		{
			len = 0;
			for(i=0; i < 3; i++)
			{
				in[i] = original[original_index];
				len = original[original_index] == '\0' ? len : len+1;
				original_index = original[original_index] == '\0' ? original_index : original_index+1;
			}
			if(len)
			{
				encode_block_base64(in, out, len);
				for(i=0; i < 4; i++)
				{
					encoded[encoded_index] = out[i];
					encoded_index++;
				}
				blocksout++;
			}
			if(blocksout >= (linesize/4))
			{
				encoded[encoded_index] = '\n';
				encoded_index++;
			}
		}
		encoded[encoded_index] = '\0';
	}
	return encoded;
}

//encode 3 8-bit binary bytes as 4 '6-bit' characters
void encode_block_base64( unsigned char in[3], unsigned char out[4], int len )
{
    out[0] = cb64[ in[0] >> 2 ];
    out[1] = cb64[ ((in[0] & 0x03) << 4) | ((in[1] & 0xf0) >> 4) ];
    out[2] = (unsigned char) (len > 1 ? cb64[ ((in[1] & 0x0f) << 2) | ((in[2] & 0xc0) >> 6) ] : '=');
    out[3] = (unsigned char) (len > 2 ? cb64[ in[2] & 0x3f ] : '=');
}

#ifndef STRING_UTIL_H

void to_lowercase(char* str)
{
	int i;
	for(i = 0; str[i] != '\0'; i++)
	{
		str[i] = tolower(str[i]);
	}
}

char* dynamic_strcat(int num_strs, ...)
{
	
	va_list strs;
	int new_length = 0;
	
	va_start(strs, num_strs);
	int i;
	for(i=0; i < num_strs; i++)
	{
		char* next_arg = va_arg(strs, char*);
		if(next_arg != NULL)
		{
			new_length = new_length + strlen(next_arg);
		}
	}
	va_end(strs);
	
	char* new_str = malloc((1+new_length)*sizeof(char));
	va_start(strs, num_strs);
	int next_start = 0;
	for(i=0; i < num_strs; i++)
	{
		char* next_arg = va_arg(strs, char*);
		if(next_arg != NULL)
		{
			int next_length = strlen(next_arg);
			memcpy(new_str+next_start,next_arg, next_length);
			next_start = next_start+next_length;
		}
	}
	new_str[next_start] = '\0';
	
	return new_str;
}
#endif


//returns data upon success, NULL on failure
void* initialize_connection_http(char* host, int port)
{
	int *socket = (int*)malloc(sizeof(int));
	*socket	= tcp_connect(host, port);
	if(*socket >= 0)
	{	
		return socket;
	}
	else
	{
		free(socket);
		return NULL;
	}
}
int read_http(void* connection_data, char* read_buffer, int read_length)
{
	int* socket = (int*)connection_data;
	return read(*socket, read_buffer, read_length);
}
int write_http(void* connection_data, char* data, int data_length)
{
	int* socket = (int*)connection_data;
	return write(*socket, data, data_length);
}

void destroy_connection_http(void* connection_data)
{
	if(connection_data != NULL)
	{
		int* socket = (int*)connection_data;
		free(socket);
	}
}

#ifdef HAVE_SSL

void* initialize_connection_https(char* host, int port)
{
	ssl_connection_data* connection_data = NULL;
	int socket = tcp_connect(host, port);
	if(socket >= 0)
	{
		int initialized = -1;
		SSL* ssl = NULL;
		SSL_CTX *ctx = NULL;
		#ifdef USE_OPENSSL
			SSL_library_init();
			SSL_load_error_strings();
			SSL_METHOD *meth = SSLv23_method();
			ctx = SSL_CTX_new(meth);
    			ssl = SSL_new(ctx);
			SSL_set_fd(ssl, socket);
			initialized = SSL_connect(ssl);
			if(initialized >=0)
			{
				connection_data = (ssl_connection_data*)malloc(sizeof(ssl_connection_data));
				connection_data->socket = socket;
				connection_data->ctx = ctx;
				connection_data->ssl = ssl;
			}
			else
			{
				close(socket);
				SSL_free(ssl);
				SSL_CTX_free(ctx);
			}
			//would check cert here if we were doing it
		#endif

		#ifdef USE_MATRIXSSL
			matrixSslOpen();
			initialized = -1;
		       	if(matrixSslOpen() >= 0)
			{
				int key_test = matrixSslReadKeys(&ctx, NULL, NULL, NULL, NULL);
				ssl = SSL_new(ctx, 0);
				if(ssl != NULL && key_test >=0)
				{
					SSL_set_fd(ssl, socket);
					//would define cert checker here if we were doing it
					initialized = SSL_connect(ssl, NULL, NULL); 
				}
			}
		#endif
		if(initialized >= 0)
		{
			connection_data = (ssl_connection_data*)malloc(sizeof(ssl_connection_data));
			connection_data->socket = socket;
			connection_data->ssl = ssl;
			connection_data->ctx = ctx;
		}
		else
		{
			close(socket);
			SSL_free(ssl);
			SSL_CTX_free(ctx);
		}
	}

	return connection_data;
}
int read_https(void* connection_data, char* read_buffer, int read_length)
{
	ssl_connection_data *cd = (ssl_connection_data*)connection_data;
	return SSL_read(cd->ssl, read_buffer, read_length);

}
int write_https(void* connection_data, char* data, int data_length)
{
	ssl_connection_data *cd = (ssl_connection_data*)connection_data;
	return SSL_write(cd->ssl, data, data_length);
}
void destroy_connection_https(void* connection_data)
{
	if(connection_data != NULL)
	{
		ssl_connection_data *cd = (ssl_connection_data*)connection_data;
		close(cd->socket);
		SSL_free(cd->ssl);
		SSL_CTX_free(cd->ctx);
		free(cd);
	}
}

#endif //end HAVE_SSL definitions

