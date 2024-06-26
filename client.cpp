#include <iostream>
#include <cstring>
#include <vector>
#include <thread>
#include <algorithm>
#include <set>
#include <unordered_map>
#include <utility>
#include <csignal>
#include <climits>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>

#define MAX_MESSAGE_LENGTH 256

// ANSI escape codes for text color
#define RESET "\033[0m"
#define BLACK "\033[30m"   /* Black */
#define RED "\033[31m"	   /* Red */
#define GREEN "\033[32m"   /* Green */
#define YELLOW "\033[33m"  /* Yellow */
#define BLUE "\033[34m"	   /* Blue */
#define MAGENTA "\033[35m" /* Magenta */
#define CYAN "\033[36m"	   /* Cyan */
#define WHITE "\033[37m"   /* White */

using IntPair = std::pair<int, int>;
// first is timestamp, second is system number

int localTimestamp;
int sockfd1, sockfd2, sockfd3;
int this_system_no;
int requestTimeStamp;
int critical_section_held_by; // only for system 1
std::set<IntPair> requests;
std::unordered_map<int, int> requestLookup;
std::vector<IntPair> replies;

bool startsWith(const std::string &fullString, const std::string &starting)
{
	if (fullString.length() >= starting.length())
	{
		return (0 == fullString.compare(0, starting.length(), starting));
	}
	else
	{
		return false;
	}
}

int getTimestamp(const std::string &msgString)
{
	size_t pos = msgString.find('\n');
	std::string timestampPart = msgString.substr(pos + 1);
	try
	{
		return std::stoi(timestampPart);
	}
	catch (const std::invalid_argument &e)
	{
		return -1; // Or any other default value
	}
}

void broadcastMessage(std::string msg)
{
	if (this_system_no != 1)
		write(sockfd1, msg.c_str(), MAX_MESSAGE_LENGTH);
	if (this_system_no != 2)
		write(sockfd2, msg.c_str(), MAX_MESSAGE_LENGTH);
	if (this_system_no != 3)
		write(sockfd3, msg.c_str(), MAX_MESSAGE_LENGTH);
}

void checkToEnterCriticalSection()
{
	IntPair topRequest = *(requests.begin());
	if (topRequest.second != this_system_no)
		return;
	if (replies.size() < 2)
		return;
	for (const auto &r : replies)
	{
		if (r.first <= requestTimeStamp)
			return;
	}
	replies.clear();
	std::string msg = "enter critical section\n";
	broadcastMessage(msg);
	if (this_system_no == 1)
		critical_section_held_by = 1;
	std::cout << GREEN << "System " << this_system_no << " (you) entered critical section" << RESET << std::endl;
}

void readThread(int readsockfd, int s_no)
{
	char buffer[MAX_MESSAGE_LENGTH];
	while (true)
	{
		memset(buffer, 0, sizeof(buffer));
		read(readsockfd, buffer, MAX_MESSAGE_LENGTH);
		if (strlen(buffer) == 0)
			continue;
		std::string str(buffer);
		if (startsWith(str, "check critical section"))
		{
			std::string checkMsg;
			if (critical_section_held_by == 0)
				checkMsg = "critical section is not held by anyone";
			else
				checkMsg = "critical section is held by " + std::to_string(critical_section_held_by);
			write(readsockfd, checkMsg.c_str(), MAX_MESSAGE_LENGTH);
			continue;
		}
		else if (startsWith(str, "critical section is "))
		{
			std::cout << MAGENTA << str << RESET << std::endl;
			continue;
		}
		else if (startsWith(str, "enter critical section"))
		{
			if (this_system_no == 1)
			{
				critical_section_held_by = s_no;
			}
			std::cout << MAGENTA << "System " << s_no << " entered critical section" << RESET << std::endl
					  << RESET;
			continue;
		}
		int recvTimestamp = getTimestamp(str);
		localTimestamp = std::max(recvTimestamp, localTimestamp) + 1;
		if (startsWith(str, "request critical section"))
		{
			std::cout << CYAN << "Critical section access request from system no. " << s_no << RESET << std::endl;
			requests.insert({recvTimestamp, s_no});
			requestLookup[s_no] = recvTimestamp;
			std::string replyMsg = "reply to request\n" + std::to_string(localTimestamp);
			write(readsockfd, replyMsg.c_str(), MAX_MESSAGE_LENGTH);
		}
		else if (startsWith(str, "release critical section"))
		{
			std::string strReply;
			if ((*(requests.begin())).second == s_no)
				strReply = "Critical section access release from system no. " + std::to_string(s_no) + "\n";
			else
				strReply = "Critical section request withdrawn from system no. " + std::to_string(s_no) + "\n";
			std::cout << GREEN << strReply << RESET;
			requests.erase({requestLookup[s_no], s_no});
			requestLookup.erase(s_no);
			if (this_system_no == 1 && critical_section_held_by == s_no)
				critical_section_held_by = 0;
			checkToEnterCriticalSection();
		}
		else if (startsWith(str, "reply to request"))
		{
			std::cout << MAGENTA << "Critical section access reply from system no. " << s_no << RESET << std::endl;
			replies.push_back({recvTimestamp, s_no});
			checkToEnterCriticalSection();
		}
		else
		{
			std::cout << "Error: unexpected message" << std::endl;
		}

		std::cout << YELLOW << "Your current local timestamp is " << localTimestamp << RESET << std::endl;
	}
}

void writeThread()
{
	std::cout << YELLOW;
	std::cout << ("Commands:\n");
	std::cout << ("local event              ---- ") << RESET << ("performs a local event\n") << YELLOW;
	std::cout << ("request critical section ---- ") << RESET << ("requests access to the critical section\n") << YELLOW;
	std::cout << ("release critical section ---- ") << RESET << ("releases access from the critical section if you have access, else removes request\n") << YELLOW;
	std::cout << ("check critical section   ---- ") << RESET << ("returns the system which currently has access to the critical section\n") << YELLOW;
	std::cout << ("view requests            ---- ") << RESET << ("view request queue of this client along with timestamps\n") << YELLOW;
	std::cout << ("view replies             ---- ") << RESET << ("view reply queue of this client along with timestamps\n") << YELLOW;
	std::cout << ("close                    ---- ") << RESET << ("closes the connection\n") << YELLOW;
	std::cout << RESET;
	while (true)
	{
		std::string str;
		std::getline(std::cin, str);
		if (str.empty())
			continue;
		if (startsWith(str, "close"))
		{
			std::cout << "---------------" << std::endl;
			pid_t pid = getpid();
			kill(pid, SIGTERM);
		}
		else if (startsWith(str, "local event"))
		{
			std::cout << "---------------" << std::endl;
			localTimestamp++;
			std::cout << YELLOW << "Your current local timestamp is " << localTimestamp << RESET << std::endl;
		}
		else if (startsWith(str, "request critical section"))
		{
			std::cout << "---------------" << std::endl;
			if (requestLookup.find(this_system_no) != requestLookup.end())
			{
				if ((*(requests.begin())).second == this_system_no)
					std::cout << RED << "You already have critical section! " << RESET << std::endl;
				else
					std::cout << RED << "You already have requested critical section! " << RESET << std::endl;
				continue;
			}
			localTimestamp++;
			std::cout << YELLOW << "Your current local timestamp is " << localTimestamp << RESET << std::endl;
			std::string msg = "request critical section\n" + std::to_string(localTimestamp);
			broadcastMessage(msg);
			requests.insert({localTimestamp, this_system_no});
			requestLookup[this_system_no] = localTimestamp;
			requestTimeStamp = localTimestamp;
		}
		else if (startsWith(str, "release critical section"))
		{
			std::cout << "---------------" << std::endl;
			if (requestLookup.find(this_system_no) == requestLookup.end())
			{
				std::cout << RED << "You are not using Critical section or requested it earlier!" << RESET << std::endl;
				replies = std::vector<IntPair>();
				continue;
			}
			localTimestamp++;
			std::cout << YELLOW << "Your current local timestamp is " << localTimestamp << RESET << std::endl;
			std::string msg = "release critical section\n" + std::to_string(localTimestamp);
			broadcastMessage(msg);
			requests.erase({requestLookup[this_system_no], this_system_no});
			requestLookup.erase(this_system_no);
			requestTimeStamp = INT_MAX;
			if (this_system_no == 1 && critical_section_held_by == 1)
				critical_section_held_by = 0;
			replies = std::vector<IntPair>();
		}
		else if (startsWith(str, "check critical section"))
		{
			std::cout << "---------------" << std::endl;
			if (this_system_no == 1)
			{
				if (critical_section_held_by == 0)
					std::cout << GREEN << "critical section is not held by anyone" << RESET << std::endl;
				else
					std::cout << MAGENTA << "critical section is held by system" << critical_section_held_by << RESET << std::endl;
			}
			else
			{
				std::string msg = "check critical section\n";
				write(sockfd1, msg.c_str(), MAX_MESSAGE_LENGTH);
			}
		}
		else if (startsWith(str, "view requests"))
		{
			std::cout << "---------------" << std::endl;
			for (const auto &r : requests)
			{
				std::cout << BLUE << "System " << r.second << ", Timestamp: " << r.first << "\n"
						  << RESET;
			}
		}
		else if (startsWith(str, "view replies"))
		{
			std::cout << "---------------" << std::endl;
			for (const auto &r : replies)
			{
				std::cout << BLUE << "System " << r.second << ", Timestamp: " << r.first << "\n"
						  << RESET;
			}
		}
		else
		{
			std::cout << "---------------" << std::endl;
			std::cout << RED << "Invalid command" << std::endl
					  << RESET;
		}
	}
}

void printFirstLocalIP()
{
	struct ifaddrs *addrs, *tmp;
	getifaddrs(&addrs);
	tmp = addrs;

	while (tmp)
	{
		if (tmp->ifa_addr && tmp->ifa_addr->sa_family == AF_INET)
		{
			struct sockaddr_in *pAddr = (struct sockaddr_in *)tmp->ifa_addr;
			std::string ip = inet_ntoa(pAddr->sin_addr);
			if (ip != "127.0.0.1")
			{
				std::cout << ip;
				break;
			}
		}
		tmp = tmp->ifa_next;
	}

	freeifaddrs(addrs);
}

int connectToIPAddress()
{
	std::cout << GREEN << "Enter IP Address and Port no: (IP:PORT format) " << RESET << std::endl;
	std::string ip_string;
	std::cin >> ip_string;
	size_t pos = ip_string.find(':');
	std::string ip = ip_string.substr(0, pos);
	std::string port_string = ip_string.substr(pos + 1);
	int port_number = std::stoi(port_string);
	// Create socket
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1)
	{
		std::cerr << RED << "Failed to create socket" << RESET << std::endl;
		return -1;
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(ip.c_str());
	addr.sin_port = htons(port_number);
	if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0)
	{
		std::cerr << RED << "Invalid address: Address not supported" << RESET << std::endl;
		close(sockfd);
		return -1;
	}

	if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		std::cerr << RED << "Connection Failed" << RESET << std::endl;
		close(sockfd);
		return -1;
	}

	std::cout << GREEN << "Connected to " << ip << " on port " << port_number << RESET << std::endl;
	return sockfd;
}

int main()
{
	localTimestamp = 0;
	struct sockaddr_in addr;
	int addrlen = sizeof(addr);
	std::cout << "Enter this system number (1, 2, or 3): " << RESET;
	std::cin >> this_system_no;

	if (this_system_no < 1 || this_system_no > 3)
	{
		std::cout << "this_system_no must be between 1 and 3.\n";
		return 0;
	}

	if (this_system_no < 3)
	{
		int sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd == -1)
		{
			perror("Socket creation failed");
			exit(EXIT_FAILURE);
		}
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(0);		   // Port number
		addr.sin_addr.s_addr = INADDR_ANY; // Bind to any address

		if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
		{
			std::cerr << "Failed to bind socket" << std::endl;
			close(sockfd);
			exit(EXIT_FAILURE);
		}
		if (listen(sockfd, 3) < 0)
		{
			perror("listen");
			exit(EXIT_FAILURE);
		}
		getsockname(sockfd, (struct sockaddr *)&addr, (socklen_t *)&addrlen);
		if (this_system_no == 1)
			sockfd1 = sockfd;
		else
			sockfd2 = sockfd;
	}

	if (this_system_no == 1)
	{
		std::cout << "Connect second and third system to this one in the correct order.\n";
		std::cout << "IP Address: ";
		printFirstLocalIP();
		std::cout << ":" << ntohs(addr.sin_port) << std::endl;
		sockfd2 = accept(sockfd1, NULL, NULL);
		if (sockfd2 < 0)
		{
			perror("accept");
			exit(EXIT_FAILURE);
		}
		sockfd3 = accept(sockfd1, NULL, NULL);
		if (sockfd3 < 0)
		{
			perror("accept");
			exit(EXIT_FAILURE);
		}
	}

	if (this_system_no == 2)
	{
		std::cout << "Connect to the first sytem by providing IP Address and Port no.\n";
		sockfd1 = connectToIPAddress();

		std::cout << "Connect third system to this one.\n";
		std::cout << "IP Address: ";
		printFirstLocalIP();
		std::cout << ":" << ntohs(addr.sin_port) << std::endl;
		sockfd3 = accept(sockfd2, NULL, NULL);
		if (sockfd3 < 0)
		{
			perror("accept");
			exit(EXIT_FAILURE);
		}
	}

	if (this_system_no == 3)
	{
		std::cout << "Connect to the first sytem by providing IP Address and Port no.\n";
		sockfd1 = connectToIPAddress();

		std::cout << "Connect to the second sytem by providing IP Address and Port no.\n";
		sockfd2 = connectToIPAddress();

		sockfd3 = -1;
	}

	std::cout << "All systems connected to each other." << std::endl;

	std::thread t, t1, t2, t3;
	t = std::thread(writeThread);

	if (this_system_no != 1)
		t1 = std::thread(readThread, sockfd1, 1);
	if (this_system_no != 2)
		t2 = std::thread(readThread, sockfd2, 2);
	if (this_system_no != 3)
		t3 = std::thread(readThread, sockfd3, 3);

	t.join();
	if (this_system_no != 1)
		t1.join();
	if (this_system_no != 2)
		t2.join();
	if (this_system_no != 3)
		t3.join();

	return 0;
}
