// server
// Jason Clark 800617442
#include <dirent.h>
#include <ctime>
#include <iostream>
#include <algorithm>
#include <fstream>
#include <math.h>
#include <string>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#define BACKLOG 10
#define MAXBUFLEN 1000
#define PRIMARY_DIRECTORY "db"
using namespace std;

struct client_info
{
    int flag;
    string sender;
    string receiver;
    string data;
    string directory;
};

void smtp_server(char* smtp_port);
void http_server(char* http_port);
string smtp_message_builder(char* in_buf, client_info *client_state, int index, string s_client_addr);
string http_message_builder(char* in_buf);
void read_config(char *argv[], char* smtp_port, char* http_port);
string get_hostname_str();
void sigchld_handler(int s);
void *get_in_addr(struct sockaddr *sa);
bool validate_domain(string domain);
bool validate_directory(string directory);
void write_email(char* in_buf, client_info* client_state, int index);
string read_file(string filename);
void mark_as_read(string data, string path);

int main(int argc, char *argv[])
{
    char smtp_port[6]; 
    char http_port[6];
    string email_directory(PRIMARY_DIRECTORY);
    
    if(argc != 2)
    {
        perror("Error, only one runtime argument accepted: server.conf");
        return 0;
    }
    
    validate_directory(email_directory);
    validate_directory(email_directory+"/viewed");
    read_config(argv, smtp_port, http_port);

    printf("Starting servers...\n");
    if(fork() == 0)
        smtp_server(smtp_port);
    else
        http_server(http_port);

    cout << "\nServer has finished execution...\n";
    return 0;
}

void smtp_server(char* smtp_port)
{
    fd_set master;   
    fd_set read_fds;  
    int fdmax;       

    int listener;    
    int newfd;       
    struct sockaddr_storage remoteaddr; 
    struct sockaddr_in client_address;

    socklen_t addrlen;
    client_info* client_state; 

    char in_buf[MAXBUFLEN];    
    int nbytes;

    char remoteIP[INET6_ADDRSTRLEN];

    int yes=1;        
    int i, j, rv;

    struct addrinfo hints, *ai, *p;
    FD_ZERO(&master);    
    FD_ZERO(&read_fds);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    
    if ((rv = getaddrinfo(NULL, smtp_port, &hints, &ai)) != 0) {
        fprintf(stderr, "smtp server: %s\n", gai_strerror(rv));
        exit(1);
    }
    
    for(p = ai; p != NULL; p = p->ai_next) 
    {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) 
        { 
            continue;
        }
        
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) 
        {
            close(listener);
            continue;
        }

        break;
    }

    if (p == NULL) 
    {
        fprintf(stderr, "smtp server: failed to bind\n");
        exit(2);
    }

    freeaddrinfo(ai);

    if (listen(listener, BACKLOG) == -1) 
    {
        perror("listen");
        exit(3);
    }

    FD_SET(listener, &master);
    fdmax = listener; 

    
    client_state = new client_info[fdmax];
    for(int i=0; i < fdmax; i++)
    {
        client_state[i].flag = 0;
    }
    
    for(;;) 
    {
        char out_buf[MAXBUFLEN];
        cout << "----------" << endl;
        
        read_fds = master; 
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) 
        {
            perror("select");
            exit(4);
        }

        for(i = 0; i <= fdmax; i++) 
        {
            if (FD_ISSET(i, &read_fds)) 
            { 
                if (i == listener) 
                {
                    addrlen = sizeof remoteaddr;
                    newfd = accept(listener, (struct sockaddr *)&remoteaddr, &addrlen);

                    if (newfd == -1) 
                    {
                        perror("accept");
                    } 
                    else 
                    {
                        FD_SET(newfd, &master); 
                        if (newfd > fdmax) 
                        {   
                            fdmax = newfd;
                            // used to track user
                            client_info *temp = new client_info[fdmax];                        
                            for(int k = 0; k < fdmax-1; k++)
                            {   
                                temp[k].flag = client_state[k].flag;
                            }
                            temp[fdmax-1].flag = 0;
                            temp[fdmax-1].sender = "";
                            temp[fdmax-1].receiver = "";
                            temp[fdmax-1].data = "";
                            temp[fdmax-1].directory = "";
                            
                            
                            delete[] client_state;
                            client_state = temp;
                        }
                        printf("smtp server: new connection: %s socket %d\n", 
                        inet_ntop(remoteaddr.ss_family, get_in_addr((struct sockaddr*)&remoteaddr), remoteIP, INET6_ADDRSTRLEN),newfd);
                    }
                } 
                else 
                {
                    // handle data from a client
                    if ((nbytes = recv(i, in_buf, sizeof in_buf, 0)) <= 0) 
                    {
                        if (nbytes == 0) 
                        {
                            printf("smtp server: socket %d hung up\n", i);
                        } 
                        else 
                        {
                            perror("recv");
                        }
                        close(i); 
                        FD_CLR(i, &master); 
                        client_state[i-1].flag = 0;
                    } 
                    else 
                    {
                        addrlen = sizeof(client_address);
                        int t = getpeername(i,(struct sockaddr*)&client_address, &addrlen);
                        string s_client_addr = inet_ntoa(client_address.sin_addr);
                        cout << "smtp server:" << endl;
                        cout << "client:(" << s_client_addr << ")\n";

                        string msg_out = smtp_message_builder(in_buf, client_state, i-1, s_client_addr);

                        char out_buf[msg_out.size()];
                        strcpy(out_buf, msg_out.c_str());
                
                        if (send(i, out_buf, sizeof out_buf, 0) == -1) 
                        {
                            perror("send");
                        }
                    }
                }
            } 
        } 
    } 
    
    return;    
    
}

void http_server(char* http_port)
{       
    fd_set master;    
    fd_set read_fds;  
    int fdmax;        

    int listener;     
    int newfd;        
    struct sockaddr_storage remoteaddr; 
    struct sockaddr_in client_address;

    socklen_t addrlen;

    char in_buf[MAXBUFLEN];  
    int nbytes;

    char remoteIP[INET6_ADDRSTRLEN];

    int yes=1;    
    int i, j, rv;

    struct addrinfo hints, *ai, *p;
    FD_ZERO(&master);    
    FD_ZERO(&read_fds);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    
    if ((rv = getaddrinfo(NULL, http_port, &hints, &ai)) != 0) {
        fprintf(stderr, "smtp server: %s\n", gai_strerror(rv));
        exit(1);
    }
    
    for(p = ai; p != NULL; p = p->ai_next) 
    {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) 
        { 
            continue;
        }
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) 
        {
            close(listener);
            continue;
        }
        break;
    }

    if (p == NULL) // Not bound
    {
        fprintf(stderr, "http server: failed to bind\n");
        exit(2);
    }

    freeaddrinfo(ai); 

    if (listen(listener, BACKLOG) == -1) 
    {
        perror("listen");
        exit(3);
    }

    FD_SET(listener, &master);
    fdmax = listener; 

    for(;;) 
    {
        char out_buf[MAXBUFLEN];

        cout << "----------" << endl;
        
        read_fds = master;
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) 
        {
            perror("select");
            exit(4);
        }

        // run through the existing connections looking for data to read
        for(i = 0; i <= fdmax; i++) 
        {
            if (FD_ISSET(i, &read_fds)) 
            { 
                if (i == listener) 
                {
                    // handle new connections
                    addrlen = sizeof remoteaddr;
                    newfd = accept(listener, (struct sockaddr *)&remoteaddr, &addrlen);

                    if (newfd == -1) 
                    {
                        perror("accept");
                    } 
                    else 
                    {
                        FD_SET(newfd, &master); 
                        if (newfd > fdmax) 
                        {    
                            fdmax = newfd;
                        }
                        printf("http server: new connection: %s socket %d\n", 
                        inet_ntop(remoteaddr.ss_family, get_in_addr((struct sockaddr*)&remoteaddr), remoteIP, INET6_ADDRSTRLEN),newfd);
                    }
                } 
                else 
                {
                    // handle data from a client
                    if ((nbytes = recv(i, in_buf, sizeof in_buf, 0)) <= 0) 
                    {
                        // got error or connection closed by client
                        if (nbytes == 0)
                        {
                            printf("http server: socket %d hung up\n", i);
                        } 
                        else 
                        {
                            perror("recv");
                        }
                        close(i);
                        FD_CLR(i, &master);
                    } 
                    else 
                    {
                        addrlen = sizeof(client_address);
                        int t = getpeername(i,(struct sockaddr*)&client_address, &addrlen);
                        string s_client_addr = inet_ntoa(client_address.sin_addr);
                        cout << "http server:" << endl;
                        cout << "client:(" << s_client_addr << ")\n";

                        string msg_out = http_message_builder(in_buf);
                        
                        char out_buf[msg_out.size()];
                        strcpy(out_buf, msg_out.c_str());
                
                        if (send(i, out_buf, sizeof out_buf, 0) == -1) 
                        {
                            perror("send");
                        }
                    }
                }
            } 
        }
    }

    return;        
}

string smtp_message_builder(char* in_buf, client_info* client_state, int index, string s_client_addr)
{
    if(client_state[index].flag == 4)
    {
        write_email(in_buf, client_state, index);
        client_state[index].flag = 1;
        return "250 OK\n";
    }

    printf("msg:(%s)\n", in_buf);

    struct str_list
    {
        string data;
        str_list* next;
    };

    struct str_list* empty_str_list = new str_list;
    struct str_list* temp = new str_list;
    struct str_list *msg_in_lst = temp;

    string s_msg_in = in_buf;
    string delimiter = " ";
    string token;
    size_t position = 0;

    string msg_out = "";

    // populate msg_in_lst with (delimiter) seperated strings
    while((position = s_msg_in.find(delimiter)) != std::string::npos) 
    {
        token = s_msg_in.substr(0, position);// keep 

        temp->data = token;
        temp->next = new str_list;
        temp = temp->next;
        s_msg_in.erase(0, position + delimiter.length()); 
    }
    temp->data = s_msg_in; // store the remaining string

    if(msg_in_lst->data == "HELO")  // client_state: 0->1
    {
        if(msg_in_lst->next != NULL && msg_in_lst->next != empty_str_list)
        {
            if(msg_in_lst->next->data == get_hostname_str())
            {
                if(client_state[index].flag == 0) // new connector
                {
                    client_state[index].flag = 1;
                    msg_out += "220 HELO ";
                    msg_out += s_client_addr;
                    msg_out += "\n";

                }
                else // Client already connected
                {
                    msg_out += "220 HELO again";
                    msg_out += s_client_addr;
                    msg_out += "...\n";
                }
            }
            else // Invalid hostname
            {
                msg_out += "501 invalid hostname (try: HELO ";
                msg_out += get_hostname_str();
                msg_out += ")\n";
            }
        }
        else // No hostname provided
        {
            msg_out += "501 no hostname provided (try: HELO ";
            msg_out += get_hostname_str();
            msg_out += ")\n";
        }
    }
    else if(client_state[index].flag > 0) // client_state: >0
    {
        if(msg_in_lst->data == "MAIL") // "MAIL FROM:"
        {
            if(msg_in_lst->next != NULL && msg_in_lst->next != empty_str_list)
            {
                size_t position_2 = 0;
                position = msg_in_lst->next->data.find(":");
                token = msg_in_lst->next->data.substr(0, position);
                if(token == "FROM")
                {
                    position = msg_in_lst->next->data.find("<");
                    position_2 = msg_in_lst->next->data.find(">");
                    if(position < msg_in_lst->next->data.size() && position_2 < msg_in_lst->next->data.size()) // < and > found
                    {
                        token = msg_in_lst->next->data.substr(position+1, (position_2-position)-1);
                    
                        if(validate_domain(token) == true) // valid domain ---------------- SUCCESS
                        {
                            client_state[index].flag = 2;
                            client_state[index].sender = token;
                            client_state[index].receiver = "";
                            client_state[index].data = "";
                            client_state[index].directory = "";

                            msg_out += "250 OK\n";
                        }
                        else // invalid <domain>
                        {
                                msg_out += "550 Action not taken, ";//invalid domain (TRY user@447m22.edu)\n";
                                msg_out += token;
                                msg_out += " not valid domain\n";
                        }
                    }
                    else // didnt find < or > (possible space included)
                    {
                        msg_out += "501 Syntax error, invalid argument after MAIL (FROM:<reverse-path>)(NO SPACES ALLOWED AFTER FROM)\n";
                    }
                }
                else // wrong information after MAIL
                {
                    msg_out += "501 Syntax error, invalid argument after MAIL (FROM:<reverse-path>)\n";
                }
            }
            else // No information after mail
            {
                msg_out += "501 Syntax error, arguments expected after MAIL (FROM:<reverse-path)\n";
            }
        }
        else if(msg_in_lst->data == "RCPT") // "RCPT TO:" 
        {
            if(client_state[index].flag > 1)  // completed MESG FROM
            {
                if(msg_in_lst->next != NULL && msg_in_lst->next != empty_str_list)
                {
                    size_t position_2 = 0;
                    position = msg_in_lst->next->data.find(":");
                    token = msg_in_lst->next->data.substr(0, position);
                    if(token == "TO")
                    {
                        position = msg_in_lst->next->data.find("<");
                        position_2 = msg_in_lst->next->data.find(">");
                        if(position < msg_in_lst->next->data.size() && position_2 < msg_in_lst->next->data.size()) // < and > found
                        {
                            token = msg_in_lst->next->data.substr(position+1, (position_2-position)-1);
                            if(validate_domain(token) == true) // valid domain
                            {
                                position_2 = msg_in_lst->next->data.find("@");
                                token = msg_in_lst->next->data.substr(position+1, (position_2 - position)-1);
                                //cout << "token: " << token << endl;
                                string email_directory(PRIMARY_DIRECTORY);
                                email_directory+="/";
                                email_directory+=token;
                                if(validate_directory(email_directory) == true) // valid directory ------------ SUCCESS
                                {
                                    //cout << "em dir" << email_directory << endl;
                                    client_state[index].flag = 3;
                                    client_state[index].receiver = token;
                                    client_state[index].data = "";
                                    client_state[index].directory = email_directory;
                                    msg_out += "250 OK\n";
                                }
                                else //invalid directory
                                {
                                    msg_out += "550 Action not taken, ";
                                    msg_out += token;
                                    msg_out += " not valid directory\n";
                                }
                            }
                            else // invalid <domain>
                            {
                                msg_out += "550 Action not taken, ";//invalid domain (TRY user@447m22.edu)\n";
                                msg_out += token;
                                msg_out += " not valid domain\n";
                            }
                        }
                        else // didnt find < or > (possible space included)
                        {
                            msg_out += "501 Syntax error, invalid argument after RCPT (TO:<reverse-path>)(NO SPACES ALLOWED AFTER TO:)\n";
                        }
                    }
                    else // wrong information after RCPT
                    {
                        msg_out += "501 Syntax error, invalid argument after RCPT (TO:<reverse-path>)\n";
                    }
                }
                else // No information after RCTP
                {
                    msg_out += "501 Syntax error, arguments expected after RCPT (TO:<reverse-path)\n";
                }
            }
            else // MAIL FROM before RCPT TO
            {
                msg_out += "503 bad sequence (MAIL FROM before RCPT TO)\n";
            }
        }
        
        else if(msg_in_lst->data == "DATA") // "DATA"
        {
            if(client_state[index].flag > 2)
            {
                msg_out += "354 Start mail input; end with <CRLF>.<CRLF>\n";
                client_state[index].flag = 4;
            }
            else
            {
                msg_out += "503 bad sequence (RCPT TO before DATA)\n";
            }
        }
        else if(msg_in_lst->data == "HELP") // "HELP"
        {
            if(msg_in_lst->next != NULL && msg_in_lst->next != empty_str_list)
            {
                if(msg_in_lst->next->data == "HELP")
                {
                    msg_out+= "250 HELP\n";
                    msg_out+= "(HELP [command]CRLF)\n";
                    msg_out+= "example: HELP MAIL\n";
                }
                else if(msg_in_lst->next->data == "MAIL")
                {
                    msg_out += "250 HELP\n";
                    msg_out += "(MAIL FROM:<Reverse-path>)\n";
                    msg_out += "example: (MAIL FROM:<user@447m22.edu>)\n";
                    msg_out += "note: no spaces are allowed within argument (FROM:<Reverse-path>)\n"; 
                }
                else if(msg_in_lst->next->data == "RCPT")
                {
                    msg_out += "250 HELP\n";
                    msg_out += "(RCPT TO:<forward-path>)\n";
                    msg_out += "example: (RCPT TO:<user@447m22.edu>)\n";
                    msg_out += "note: no spaces are allowed within argument (TO:<forward-path>)\n"; 
                }
                else if(msg_in_lst->next->data == "DATA")
                {
                    msg_out+= "250 HELP\n";
                }
                else if(msg_in_lst->next->data == "QUIT")
                {
                    msg_out+= "250 HELP\n";
                    msg_out += "(QUIT CLRF)\n";
                    msg_out += "example: (QUIT)\n";
                }
                else
                {
                    msg_out+= "250 HELP\n";
                    msg_out+= "helo = (HELO server-hostname CRLF)\n";
                    msg_out+= "mail = (MAIL FROM:<Reverse-path>\n";
                    msg_out+= "rcpt = (RCPT TO:<forward-path>)\n";
                    msg_out+= "data = (DATA CRLF)\n";
                    msg_out+= "help = (HELP [command]CRLF)\n";
                    msg_out+= "quit = (QUIT CRLF)\n";
                    msg_out+= "Note: CRLF is newline operator\n";
                }
            }
            else
            {
                msg_out+= "250 HELP\n";
                msg_out+= "helo = (HELO server-hostname CRLF)\n";
                msg_out+= "mail = (MAIL FROM:<Reverse-path>\n";
                msg_out+= "rcpt = (RCPT TO:<forward-path>)\n";
                msg_out+= "data = (DATA CRLF)\n";
                msg_out+= "help = (HELP [command]CRLF)\n";
                msg_out+= "quit = (QUIT CRLF)\n";
                msg_out+= "Note: CRLF is newline operator\n";
            }
        }
        else if(msg_in_lst->data == "QUIT") 
        {
            if(msg_in_lst->next == NULL) // S 221 no arguments provided
            {
                msg_out += "221 BYE ";
                msg_out += s_client_addr;
                msg_out += "\n";

                client_state[index].flag = 0;
                client_state[index].sender = "";
                client_state[index].receiver = "";
                client_state[index].data = "";
                client_state[index].directory = "";
            }
            else // F 501, arguments provided
            {
                msg_out += "501 Syntax error, no arguments accepted\n";
            }
        }
        else // command unrecognized
        {
            msg_out+= "500 Syntax error, command unrecognized\n";
        }
    }
    else // client_state: 0
    {
        msg_out += "500 syntax error, client not in sequence (try HELO ";
        msg_out += get_hostname_str();
        msg_out += ")\n";
    }
    return msg_out;
}

string http_message_builder(char* in_buf)
{
    printf("msg:\n%s\n", in_buf);
    
    struct str_list
    {
        string data;
        str_list* next;
    };

    struct str_list* temp = new str_list;
    struct str_list *msg_in_lst = temp;

    string s_msg_in = in_buf;
    string delimiter = "\n";
    string token;
    size_t position = 0;

    string msg_out = "";

    // populate msg_in_lst with (delimiter) seperated strings
    while((position = s_msg_in.find(delimiter)) != std::string::npos) 
    {
        token = s_msg_in.substr(0, position);// keep 

        temp->data = token;
        temp->next = new str_list;
        temp = temp->next;
        
        s_msg_in.erase(0, position + delimiter.length()); 
    }

    temp->data = s_msg_in; 
    
    string msg_data = msg_in_lst->data;
    position = msg_data.find(" ");
    string user_path = msg_data.substr(4, position-4);
    string http_ver = msg_data.substr(position+1, msg_data.size()-position);

    msg_data = msg_in_lst->next->data;
    position = msg_data.find(" ");
    string host_name = msg_data.substr(position+1, msg_data.size()-position);

    msg_data = msg_in_lst->next->next->data;
    position = msg_data.find(" ");
    string num_files = msg_data.substr(position+1, position - msg_data.size());

    str_list* empty_str_list = new str_list;
    empty_str_list->data = "";

    temp = empty_str_list;
    str_list* file_list = temp;
    

    const char* c_user_path = user_path.c_str();
    struct dirent *dp;
    DIR* dirp = opendir(c_user_path);

    if(dirp == NULL)
    {
        msg_out += "404, User not found\n";
        return msg_out;
    }
    else
    {
        while((dp = readdir(dirp))!= NULL)
        {
            if(dp->d_type == DT_REG)
            {
                temp->data = dp->d_name;
                temp->next = new str_list;
                temp->next->data = "";
                temp = temp->next;
            }
        }
        closedir(dirp);

        const char* c_num_files = num_files.c_str();
        bool is_number = true;

        for(char const &c : num_files)
        {
            if(isdigit(c) == 0)
            {
                is_number = false;
            }
        }
        if(is_number == false)
        {
            msg_out += http_ver; 
            msg_out += " 400 Bad Request, Count not a number\n";
        }
        else
        {   
            int msg_num = 1;
            int i_num_files = stoi(num_files);

            time_t curr_time;
            tm* cu_time;
            char timestamp[100];

            time(&curr_time);
            cu_time = localtime(&curr_time);
            strftime(timestamp, 50, "%c", cu_time);

            msg_out += http_ver; 
            msg_out += " 200 OK\n";
            msg_out += "Server: ";
            msg_out += host_name;
            msg_out += "\nLast-Modified: ";
            msg_out += timestamp;
            msg_out += "\nCount: ";
            msg_out += num_files;
            msg_out += "\nContent-Type: text/plain\n";

            position = user_path.find("/");
            token = user_path.substr(position+1, user_path.size()-position);
            position = token.find("/");
            string username = token.substr(0, position);
            string read_folder = "db/viewed/";
            read_folder += username;
            read_folder += "/";
        
            while(file_list->data != "" && i_num_files != 0)
            {
                string d1 = read_file(user_path + file_list->data);
            
                validate_directory(read_folder);
                mark_as_read(d1, read_folder);

                msg_out += "Message: ";
                msg_out += to_string(msg_num);
                msg_out += "\n\n";
                msg_out += d1;
                msg_out += "\n";

                remove((user_path + file_list->data).c_str());
                file_list = file_list->next;
                i_num_files--;
                msg_num++;
            }
            if(i_num_files == 0 && file_list->data != "")
            {
                msg_out += "No more emails to be retrieved\n";
            }
            else
            {
                msg_out += "All messages retrieved\n";
            }
        }
    }
    return msg_out;
}

void read_config(char *argv[], char* smtp_port, char* http_port)
{
    bool is_smtp = false;
    bool is_http = false;

    int count = 0;
    ifstream config;
    string line;
    string delimiter = "=";
    string args[2];
    size_t position;
    
    config.open(argv[1]);
    
    // erase the content from beginning of string to delim, including delim
    while(getline(config, line))    
    {
        position = line.find(delimiter); 
        line.erase(0, position+delimiter.length());
        args[count] = line;
        count++;
        if(count == 2)
            break;
    }
    config.close();

    strcpy(smtp_port, args[0].c_str());
    strcpy(http_port, args[1].c_str());
}

string get_hostname_str()
{
    char c_hostname[50];
    int host_flag;
    if((host_flag = gethostname(c_hostname, sizeof(c_hostname))) == -1)
    {
        perror("get_hostname_str");
        exit(EXIT_FAILURE);
    }
    string s_hostname = c_hostname;
    return s_hostname;
}

void sigchld_handler(int s)
{
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

bool validate_domain(string domain)
{
    string valid = "447m22.edu";
    string delimiter = "@";
    string token;
    size_t position = 0;

    if((position = domain.find(delimiter))!=std::string::npos)
    {
        token = domain.substr(position+1, (domain.size()-position));
        if(token == valid)
        {
            return true;
        }
        else
        {   
            return false;
        }
    }
    else
    {
        return false;
    }
}

bool validate_directory(string directory)
{
    int status = mkdir(directory.c_str(),0777);
    if(errno != EEXIST && status == -1) 
    {
        cout << "Error creating directory: " << directory << endl;
        errno = 0;
        return false;
    }
    else 
    {   
        errno = 0;
        return true;
    }
}

void write_email(char* in_buf, client_info* client_state, int index)
{
    string user_path = client_state[index].directory;
    user_path += "/";
    string file_dir = "";
    int count = 1;
    size_t len = 3;
    
    time_t curr_time;
    tm* cu_time;
    char timestamp[100];


    while(true)
    {
        string temp = user_path;
        file_dir = to_string(count);
        int precision = len - min(len, file_dir.size());
        file_dir.insert(0, precision, '0');
        file_dir+=".email";
        temp += file_dir;
        file_dir = temp;
        
        ifstream f(file_dir.c_str());
        if (!f.good())
        {
            break;
        } 
             
        if(count == 999)
        {
            break;
        }
        count++;
    }

    cout << "write to: " << file_dir << endl;

    time(&curr_time);
    cu_time = localtime(&curr_time);
    strftime(timestamp, 50, "Date:   %c", cu_time);
    ofstream file(file_dir);
    if(file.is_open())
    {
        file << timestamp << endl;
        file << "From:   " << "<" << client_state[index].sender << ">" << endl;
        file << "To:   " << "<" << client_state[index].receiver << "@447m22.edu>" << endl;
        file << in_buf;
    }
    file.close();
    return;
}   

string read_file(string filename)
{
    string all_text;
    string line;
    ifstream file(filename);
    while(getline(file, line))
    {
        all_text+= line;
        all_text+= "\n";

    }
    return all_text;
}

void mark_as_read(string data, string path)
{
    string file_dir;
    int count = 1;
    size_t len = 3;
    
    while(true)
    {
        string temp = path;
        file_dir = to_string(count);
        int precision = len - min(len, file_dir.size());
        file_dir.insert(0, precision, '0');
        file_dir+=".txt";
        temp += file_dir;
        file_dir = temp;
        
        ifstream f(file_dir.c_str());
        if (!f.good())
        {
            break;
        } 
             
        if(count == 999)
        {
            break;
        }
        count++;
    }

    ofstream file(file_dir);
    if(file.is_open())
    {
        file << data;
    }
    file.close();
    return;
}