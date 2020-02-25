//
// Created by mashaal on 11/15/19.
//
#include <iostream>
#include <cstdlib>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <sstream>
#include <iterator>
#include <fstream>
#include <experimental/filesystem>
#include <errno.h>
#include <utility>

#define DEFAULT_PORT 80
#define BUFFER_SIZE 128 * 1024
#define TIME_OUT_IN_SEC 10

using namespace std;
namespace fs = std::experimental::filesystem;

void terminateWithError(string error);
int sendAll(int s, char *buf, int *len);
vector <pair<string, string>> getRequests(string filename);
string get_http_get_request(int socket, string filepath);
string get_http_post_request(int socket, string file_to_send);
string getContentType(string file);

int get_full_request_len(string request);

int get_status_code(string request);

string get_data(string basicString);

string get_file_ext(string basicString);

string get_extension(string basicString);

void write_file(string path, string extension, string data, int size);

int get_data_size(string request);

string get_file_name(string basicString);

int main(int argc, char * argv[]) {

    int sock;
    struct sockaddr_in serverAddr;
    unsigned short serverPort;
    char * serverIP = new char[64];
    char * data ;
    int dataLen;
    if(argc == 2){
        serverIP = argv[1];
        serverPort = DEFAULT_PORT;
    }
    if(argc == 3){
        serverIP = argv[1];
        serverPort = atoi(argv[2]);
    }

    // Requests File
    string filename= "./requests.txt";
    /*cout << "Enter File Name .....\n";
    cin >> filename;*/
    vector<pair<string, string>> requests = getRequests(filename);

    // Creating Socket
    if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        terminateWithError("Client Socket ERROR");


    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(serverIP);
    serverAddr.sin_port = htons(serverPort);

    if (connect(sock, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0)
        terminateWithError("Failed to Connect") ;

    struct timeval timeout = {TIME_OUT_IN_SEC, 0};
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *) &timeout, sizeof(timeout)) < 0)
        terminateWithError("Socket Timeout settings error");

    for(int i = 0 ; i < requests.size() ; i ++){
        pair<string, string> request = requests[i];
        if(request.first.compare("GET") == 0){
            string str = get_http_get_request(sock, request.second);
            data = &str[0];
            dataLen = strlen(data);
            // request sent
            if(sendAll(sock, data, &dataLen) == -1)
                terminateWithError("Sending Error");

        }
        else{
            string str = get_http_post_request(sock, request.second);
            data = &str[0];
            dataLen = str.size();
            if(sendAll(sock, data, &dataLen) == -1)
                terminateWithError("Sending Error");
        }
    }
    bool finished = false;
    int recvMsgSize = 0;
    char buffer[BUFFER_SIZE];
    string temp_buffer = "";
    while(!finished){
        if ((recvMsgSize = recv(sock, buffer, BUFFER_SIZE, 0)) < 0) {
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
                int status_code = get_status_code(full_request);
                if(status_code == 404){
                    cout << full_request;
                }
                else{
                    cout << full_request.substr(0, full_request.find("\r\n\r\n")) << "\n";
                    string data = get_data(full_request);
                    int size = get_data_size(full_request);
                    string get_type = get_file_ext(full_request);
                    string filename = get_file_name(full_request);
                    string path = "./";
                    write_file(path, filename, data, size);
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
    close(sock);
    return 0;
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

int get_status_code(string request) {
    size_t pos = request.find("\r\n");
    string res = request.substr(0,pos);
    istringstream iss(res);
    vector<string> tokens{istream_iterator<string>{iss},
                          istream_iterator<string>{}};
    string status_code = tokens[1];
    return stoi(status_code);
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
        string status_code = tokens[1];

        if(stoi(status_code) == 404){
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

void terminateWithError(string error){
    cout << error << "\n";
    exit(-1);
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

string get_http_post_request(int socket, string file_to_send){
    string request = "POST / HTTP/1.1\r\n";
    int byte_len = fs::file_size(file_to_send);
    string type = getContentType(file_to_send);
    size_t ext_pos = file_to_send.find_last_of('/');
    string filename = file_to_send.substr(ext_pos+1);
    request = request + "Content-Type: " + type + "\r\n";
    request = request + "Content-Length: " + to_string(byte_len) + "\r\n";
    request = request + "Content-Disposition: inline; filename=\"" + filename + "\"" + "\r\n";
    request = request + "\r\n";
    std::ifstream t(file_to_send);
    std::string data((std::istreambuf_iterator<char>(t)),
                     std::istreambuf_iterator<char>());
    request = request + data;
    return request;
}

string getContentType(string file) {
    string extension = file.substr(file.find_last_of('.') + 1);
    string type = "";
    if(extension.compare("html")== 0){
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

string get_http_get_request(int socket, string filepath){
    string request = "GET " + filepath + " HTTP/1.1" + "\r\n";
    request = request + "\r\n";
    return request;
}

vector<pair<string, string>> getRequests(string filename){
    ifstream requests_file (filename);
    string line;
    vector<pair<string, string>> requests;
    if (requests_file.is_open())
    {
        while ( getline (requests_file,line) )
        {
            istringstream iss(line);
            vector<string> tokens{istream_iterator<string>{iss},
                                  istream_iterator<string>{}};
            string type = tokens[0];
            string path = tokens[1];
            if(type.compare("client_get") == 0){
                requests.push_back(make_pair("GET", path));
            }
            else{
                requests.push_back(make_pair("POST", path));
            }
        }
        requests_file.close();
    }
    else{
        terminateWithError("File Error");
    }
    return requests;
}
