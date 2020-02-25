//
// Created by mashaal on 11/15/19.
//
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <thread>
#include <pthread.h>
#include <unistd.h>
#include <vector>
#include <sstream>
#include <iterator>
#include <fstream>
#include <experimental/filesystem>
#include <errno.h>
#include <cstdlib>

#define DEFAULT_PORT 80
#define MAX_PENDING_CLIENTS 10
#define TOTAL_TIME_OUT_IN_SEC 3600
#define BUFFER_SIZE 128 * 1024

using namespace std;
namespace fs = std::experimental::filesystem;

pthread_mutex_t mtx_lock;
int currentClients = 0;

void terminateWithError(string error);

void handleClient(int clientSocket);
int sendAll(int s, char *buf, int *len);
int get_full_request_len(string request);

string get_request_type(string request);

string get_path(string request);

string getContentType(string file);

string get_not_found_response();

string get_data(string basicString);

string get_file_ext(string basicString);

string get_extension(string basicString);

void write_file(string path, string filename, string data, int size);

int get_data_size(string request);

string get_file_name(string request);

int main(int argc, char * argv[]) {
    int serverSocket;
    int clientSocket;
    struct sockaddr_in serverAddr;
    struct sockaddr_in clientAddr;
    unsigned short serverPort;
    unsigned int clientLen;

    if (pthread_mutex_init(&mtx_lock, NULL) != 0)
        terminateWithError("Mutex Initiation Failed");
    //get Server Port
    if(argc != 2){
        serverPort = DEFAULT_PORT;
    }
    else{
        serverPort = atoi(argv[1]);
    }
    //serverPort = 5097;
    //Create Server Socket
    if ((serverSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        terminateWithError("Server Socket couldn't be created");
    //Fill Address Struct
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(serverPort);
    // Bind Server Socket
    if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
             terminateWithError("Server Socket Binding ERROR");
    if (listen(serverSocket, MAX_PENDING_CLIENTS) < 0)
        terminateWithError("Server Listen ERROR");
    while(true){
        clientLen = sizeof(clientAddr);
        if ((clientSocket = accept(serverSocket, (struct sockaddr *) &clientAddr, &clientLen)) < 0)
            terminateWithError("Server Accept ERROR");
        printf("Handling client %s:%d\n", inet_ntoa(clientAddr.sin_addr), clientAddr.sin_port);
        pthread_mutex_lock(&mtx_lock);
        currentClients ++;
        pthread_mutex_unlock(&mtx_lock);
        thread thr(handleClient, clientSocket);
        thr.detach();
    }
    return 0;
}

void terminateWithError(string error){
    cout << error << "\n";
    exit(-1);
}

void handleClient(int clientSocket){
    int recvMsgSize = 0;
    char buffer[BUFFER_SIZE];
    string temp_buffer = "";
    bool finished = false;
    while(!finished){
        // dynamically timeout the connection
        struct timeval timeout;
        pthread_mutex_lock(&mtx_lock);
        timeout.tv_sec = TOTAL_TIME_OUT_IN_SEC / currentClients;
        timeout.tv_usec = TOTAL_TIME_OUT_IN_SEC % currentClients;
        if (setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, (char *) &timeout, sizeof(timeout)) < 0)
            terminateWithError("Socket Timeout settings error");
        pthread_mutex_unlock(&mtx_lock);
        // receive requests
        if ((recvMsgSize = recv(clientSocket, buffer, BUFFER_SIZE, 0)) < 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                finished = true;
            }
        }
        if(recvMsgSize > 0) {
            string  str = string(buffer, recvMsgSize);
            temp_buffer = temp_buffer + str;
            bool exist_full_requests = true;
            int len_of_request = get_full_request_len(temp_buffer);
            if(len_of_request == -1)
                exist_full_requests = false;
            while(exist_full_requests){
                string full_request = temp_buffer.substr(0, len_of_request);
                if(len_of_request >= temp_buffer.size())
                    temp_buffer = "";
                else
                    temp_buffer = temp_buffer.substr(len_of_request);
                string req_type = get_request_type(full_request);
                if(req_type.compare("GET") == 0){
                    cout << full_request.substr(0, full_request.find("\r\n\r\n")) << "\n";
                    string file_path = "." +  get_path(full_request);
                    string respone = "";
                    int byte_len = 0;
                    char * data;
                    int dataLen;
                    try {
                        byte_len = fs::file_size(file_path); // attempt to get size of a directory
                    } catch(fs::filesystem_error& e) {
                        byte_len = -1;
                    }
                    if(byte_len == -1){
                        respone = get_not_found_response();
                        dataLen = respone.size();
                    }
                    else{
                        respone = "HTTP/1.1 200 OK\r\n";
                        string type = getContentType(file_path);
                        size_t ext_pos = file_path.find_last_of('/');
                        string filename = file_path.substr(ext_pos+1);
                        respone = respone + "Content-Type: " + type + "\r\n";
                        respone = respone + "Content-Length: " + to_string(byte_len) + "\r\n";
                        respone = respone + "Content-Disposition: inline; filename=\"" + filename + "\"" + "\r\n";
                        respone = respone + "\r\n";
                        std::ifstream t(file_path);
                        std::string data((std::istreambuf_iterator<char>(t)),
                                         std::istreambuf_iterator<char>());
                        respone = respone + data;
                        dataLen = respone.size();
                    }
                    data = &respone[0];
                    sendAll(clientSocket, data, &dataLen);
                }
                else{
                    cout << full_request.substr(0, full_request.find("\r\n\r\n")) << "\n";
                    string file_path = "." + get_path(full_request);
                    string file_name = get_file_name(full_request);
                    string data = get_data(full_request);
                    string get_type = get_file_ext(full_request);
                    int size = get_data_size(full_request);
                    write_file(file_path, file_name, data, size);
                }
                len_of_request = get_full_request_len(temp_buffer);
                if(len_of_request == -1)
                    exist_full_requests = false;
            }
        }
        else{
            finished = true;
        }
    }
    pthread_mutex_lock(&mtx_lock);
    currentClients --;
    pthread_mutex_unlock(&mtx_lock);
    close(clientSocket);
}

string get_file_name(string request) {
    size_t first_idx = request.find("Content-Disposition: ");
    size_t sec_idx = request.find("\r\n", first_idx);
    string len_str = request.substr(first_idx, sec_idx-first_idx);
    istringstream isss(len_str);
    vector<string> tokens{istream_iterator<string>{isss},
                          istream_iterator<string>{}};
    string filename_field = tokens[2];
    size_t first_qt = filename_field.find_first_of('\"');
    size_t sec_qt = filename_field.find_last_of('\"');
    return filename_field.substr(first_qt+1, sec_qt-(first_qt+1));
}

int get_data_size(string request) {
    size_t first_idx = request.find("Content-Length: ");
    size_t sec_idx = request.find("\r\n", first_idx);
    string len_str = request.substr(first_idx, sec_idx-first_idx);
    istringstream isss(len_str);
    vector<string> tokens{istream_iterator<string>{isss},
                          istream_iterator<string>{}};
    string data_len = tokens[1];
    return stoi(data_len);
}

void write_file(string path, string filename, string data, int size) {
    std::ofstream file(path + filename, std::ios::binary);
    char * data_arr = &data[0];
    file.write(data_arr, size);
}

string get_file_ext(string request) {
    size_t first_idx = request.find("Content-Type: ");
    size_t sec_idx = request.find("\r\n", first_idx);
    string len_str = request.substr(first_idx, sec_idx-first_idx);
    istringstream isss(len_str);
    vector<string> tokens{istream_iterator<string>{isss},
                          istream_iterator<string>{}};
    string type = tokens[1];
    return get_extension(type);
}

string get_extension(string type) {
    string ext= "";
    if(type.compare("text/html")== 0){
        ext = ".html";
    }
    else if(type.compare("image/png") == 0){
        ext = ".png";
    }
    else if(type.compare("image/jpeg") == 0){
        ext = ".jpeg";
    }
    else if(type.compare("image/jpg") == 0){
        ext = ".jpg";
    }
    else if(type.compare("text/plain") == 0){
        ext = ".txt";
    }
    else{
        ext = ".txt";
    }
    return ext;
}

string get_data(string request) {
    size_t req_end = request.find("\r\n\r\n");
    string data = request.substr(req_end+4);
    return data;
}

string get_not_found_response() {
    string response =   "HTTP/1.1 404 Not Found\r\n";
    response = response + "\r\n";
    return response;
}

string getContentType(string file) {
    string extension = file.substr(file.find_last_of('.') + 1);
    string type = "";
    if(extension.compare("html") == 0){
        type = "text/html";
    }
    else if(extension.compare("png") == 0){
        type = "image/png";
    }
    else if(extension.compare("jpeg") == 0){
        type = "image/jpeg";
    }
    else if(extension.compare("jpg") == 0){
        type = "image/jpg";
    }
    else if(extension.compare("txt") == 0){
        type = "text/plain";
    }
    else{
        type = "text/plain";
    }
    return type;
}

string get_path(string request) {
    size_t pos = request.find("\r\n");
    string res = request.substr(0,pos);
    istringstream iss(res);
    vector<string> tokens{istream_iterator<string>{iss},
                          istream_iterator<string>{}};
    string path = tokens[1];
    return path;
}

string get_request_type(string request) {
    size_t pos = request.find("\r\n");
    string res = request.substr(0,pos);
    istringstream iss(res);
    vector<string> tokens{istream_iterator<string>{iss},
                          istream_iterator<string>{}};
    string order = tokens[0];
    if(order.compare("GET") == 0){
        return "GET";
    }
    else{
        return "POST";
    }
}

int get_full_request_len(string request) {
    size_t req_end = request.find("\r\n\r\n");
    if(req_end == string::npos){
        return -1;
    }
    else{
        size_t pos = request.find("\r\n");
        string res = request.substr(0,pos);
        istringstream iss(res);
        vector<string> tokens{istream_iterator<string>{iss},
                              istream_iterator<string>{}};
        string order = tokens[0];
        if(order.compare("GET") == 0){
            return req_end + 4;
        }
        else{
            size_t first_idx = request.find("Content-Length: ");
            size_t sec_idx = request.find("\r\n", first_idx);
            string len_str = request.substr(first_idx, sec_idx-first_idx);
            istringstream isss(len_str);
            vector<string> tokens{istream_iterator<string>{isss},
                                  istream_iterator<string>{}};
            string length = tokens[1];
            int byte_len = stoi(length);
            if(req_end + 3 + byte_len < request.size()){
                return req_end + 4 + byte_len;
            }
            else{
                return -1;
            }
        }
    }
}

int sendAll(int s, char *buf, int *len)
{
    int total = 0;
    int bytes_left = *len; // how many we have left to send
    int n;
    while(total < *len) {
        n = send(s, buf+total, bytes_left, 0);
        if (n == -1) { break; }
        total += n;
        bytes_left -= n;
    }
    *len = total; // return number actually sent here
    return n==-1?-1:0; // return -1 on failure, 0 on success
}

