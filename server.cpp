//26.09.2023 this should be update

#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/poll.h>
#include <unistd.h>
#include <errno.h>
#include <vector>
#include <fstream>
#include <cstring>
#include <queue>


#define TIMEOUT 1
#define BUFF_SIZE 4096 //Хватит ли? В программе нет обработки случая сообщения длиннее чем 4096 байт. 

#define POSTGRE_PORT 5432

int main(int argc, char *argv[]) {
	if (argc < 2) {
		std::cout << "ERROR: Enter an database ip address.";
		return -1;
	}
	int proxy = 0, cfd = 0, readbytes = 0, optval = 0, message_length = 0, postgre = 0;
	struct sockaddr_in proxy_addr = {0}, postgre_addr = {0}; //Стоит ли использовать memset? gcc ругается
	std::ofstream fout;
	if (!inet_pton(AF_INET, argv[1],&postgre_addr.sin_addr)) { //Сокет IPv4, а если база данных работает по протоколу IPv6?
		std::cout << "ERROR: Invalid address. Use IPv4.";
		return -1;
	}
	fout.open("log.txt", std::ios_base::app);
	if (!fout.is_open()) {
		std::cout << "ERROR: Log file opening is failed";
		return -1;
	}
	char buffer[BUFF_SIZE] = {'\0'};
	std::queue<int> clients_queue;
	postgre = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
	if (postgre < 0) {
		std::cout << "ERROR: " << strerror(errno);
		return -1;
	}
	postgre_addr.sin_family = AF_INET;
	postgre_addr.sin_port = htons(POSTGRE_PORT);
	if(inet_pton(AF_INET, argv[1], &postgre_addr.sin_addr) < 0) {
		std::cout << "ERROR: " << strerror(errno);
		return -1;
	}
	if (connect(postgre, (struct sockaddr *)&postgre_addr, sizeof(postgre_addr)) < 0) {
		std::cout << "ERROR: Failed to establish connect with PostgreSQL server. " << strerror(errno);
		//return -1;
	}
	proxy = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
	if (proxy < 0) {
		std::cout << "ERROR: " << strerror(errno);
		return -1;
	}
	optval = 1;
	if (setsockopt(proxy, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1){
		std::cout << "ERROR: " << strerror(errno);
		return -1;		
	}
	proxy_addr.sin_family = AF_INET;
	proxy_addr.sin_port = htons(2036); //Здесь нужно использовать универсальный порт выделенный системой, а то вдруг это будет занят
	if (bind(proxy, (struct sockaddr *)&proxy_addr, sizeof(proxy_addr)) < 0) {
		std::cout << "ERROR: " << strerror(errno);
		return -1;
	}
	if (listen(proxy, 5) < 0){
		std::cout << "ERROR: " << strerror(errno);
		return -1;
	}
	std::vector<struct pollfd> fds(2, {0,0,0});
	fds.clear();
	for(int i = 0; i < fds.size(); i++) {
		fds[i].events = POLLIN;
	}
	fds[0].fd = proxy;
	fds[1].fd = postgre;
	while(true) {
		if (poll(&fds[0], fds.size(), TIMEOUT*1000) < 0) {
			std::cout << "ERROR: " << strerror(errno);
		}
		if (fds[0].revents & POLLIN) {
			if (cfd = accept(fds[0].fd, 0, 0) > 0){
				fds.push_back(fds[0]);
				fds[fds.size() - 1].fd = cfd;
				fds[fds.size() - 1].events = POLLIN;
				//SSL handshake /*для отключения ssl шифрования нужна openssl/ssl.h но в программе нет обработки ssl!
			}
		}
		if (fds[1].revents & POLLIN) {
			readbytes = recvfrom(fds[1].fd, buffer, 4096,0,nullptr, 0);
			if (readbytes == -1) {
				std::cout << "ERROR: " << strerror(errno);
			}
			if (!clients_queue.empty()) {
				send(clients_queue.front(), buffer, readbytes, 0);
				clients_queue.pop();
			}	
		}
		for (int i = 2; i < fds.size(); i++) {                             
			if (fds[i].revents & POLLIN) {
				readbytes = recvfrom(fds[i].fd, buffer, 4096,0,nullptr, 0);
				if (readbytes == -1) {
					std::cout << "ERROR: " << strerror(errno);
				}
				for (int i = 1; i < 5; i++) {                      // Чтобы распарсить sql запрос необходимо
					message_length += static_cast<int>(buffer[i]); // узнать размер запроса в байтах
				}												   // документация говорит, что 1-ый байт - байт типа
				for (int i = 0; i < message_length - 4; i++) {     // следующие 4 байта задают длинну (-4 так длинна захватывает байты размера)
					fout << buffer[i];                             // отсюда вычисляем длинну и парсим пакет
				}                                                  // Возможные ошибки. Если запрос является первым
				fout << '\n';                                      // то первый байт отсутствует! Программа не отслеживает это.
				send(postgre, buffer, message_length + 1, 0);      // Прибавляем 1, так как сообщение должно быть полным и включать байт с типом сообщения
				clients_queue.push(fds[i].fd);
			}
			if (fds[i].revents & POLLOUT) {                       // Стоит ли закрывать соединение не передавая серверу
				if (close(fds[i].fd)) {                           // sql данное сообщение? 
					std::cout << "ERROR: " << strerror(errno);
					continue;
				} else {
					fds.erase(fds.begin() + i - 1);               
				}
			}
		}
	}
	fout.close();  
    return 0; //Ошибка в коде. ретёрна никогда не случится. В программе не предусмотрена программная обработка остановки сервера!
}


















































