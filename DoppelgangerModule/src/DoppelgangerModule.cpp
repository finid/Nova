//============================================================================
// Name        : DoppelgangerModule.cpp
// Author      : DataSoft Corporation
// Copyright   : GNU GPL v3
// Description : Nova utility for disguising a real system
//============================================================================

#include "DoppelgangerModule.h"
#include "NOVAConfiguration.h"

using namespace std;
using namespace Nova;
using namespace DoppelgangerModule;

SuspectHashTable SuspectTable;
pthread_rwlock_t lock;

//These variables used to be in the main function, changed to global to allow LoadConfig to set them
string hostAddrString, doppelgangerAddrString, honeydConfigPath;
struct sockaddr_in hostAddr, loopbackAddr;
bool isEnabled, useTerminals;

char * pathsFile = (char*)PATHS_FILE;
string homePath;

//Alarm IPC globals to improve performance
struct sockaddr_un remote;
struct sockaddr * remoteAddrPtr = (struct sockaddr *)&remote;
int connectionSocket;
int bytesRead;
u_char buf[MAX_MSG_SIZE];
Suspect * suspect = NULL;
int alarmSocket;

//GUI IPC globals to improve performance
struct sockaddr_un localIPCAddress;
struct sockaddr * localIPCAddressPtr = (struct sockaddr *)&localIPCAddress;
int IPCsock;

//constants that can be re-used
int socketSize = sizeof(remote);
socklen_t * socketSizePtr = (socklen_t*)&socketSize;

string sAlarmPort;

//Called when process receives a SIGINT, like if you press ctrl+c
void siginthandler(int param)
{
	//Clear any existing DNAT routes on exit
	//	Otherwise susepcts will keep getting routed into a black hole
	system("sudo iptables -F");
	exit(1);
}

int main(int argc, char *argv[])
{
	char suspectAddr[INET_ADDRSTRLEN];
	bzero(buf, MAX_MSG_SIZE);
	pthread_t GUIListenThread;

	SuspectTable.set_empty_key(0);
	SuspectTable.resize(INIT_SIZE_SMALL);

	signal(SIGINT, siginthandler);
	loopbackAddr.sin_addr.s_addr = INADDR_LOOPBACK;
	string novaConfig, logConfig;

	string line, prefix; //used for input checking

	//Get locations of nova files
	homePath = GetHomePath();
	novaConfig = homePath + "/Config/NOVAConfig.txt";

	//Runs the configuration loader
	LoadConfig((char*)novaConfig.c_str());

	if(!useTerminals)
	{
		//logConfig = homePath +"/Config/Log4cxxConfig.xml";
		//DOMConfigurator::configure(logConfig.c_str());
		//openlog opens a stream to syslog for logging. The parameters are as follows:
		//1st: identity; a string representing where the call is coming from.
		//2nd: options; OR all of the flags you want together to generate the correct argument
		//     LOG_CONS    = write to console IF there is an error writing to syslog
		//     LOG_PID     = Log PID with other information
		//     LOG_NDELAY  = Open connection immediately
		//     LOG_PERROR  = Print to stderror as well
		//3rd: facility; essentially what facility you want to log to. In our case Local0, other examples
		//     are AUTH or CRON, etc.
		openlog("DoppelgangerModule", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL0);
	}

	else
	{
		openlog("DoppelgangerModule", LOG_CONS | LOG_PID | LOG_NDELAY | LOG_PERROR, LOG_LOCAL0);
	}

	pthread_create(&GUIListenThread, NULL, GUILoop, NULL);
	string commandLine;
	//system commands to allow DM to function.
	commandLine = "sudo iptables -A FORWARD -i lo -j DROP";
	system(commandLine.c_str());

	commandLine = "sudo route add -host "+doppelgangerAddrString+" dev lo";
	system(commandLine.c_str());

	commandLine = "sudo iptables -t nat -F";
	system(commandLine.c_str());

    int len;
    struct sockaddr_un remote;

	//Builds the key path
	string key = KEY_ALARM_FILENAME;
	key = homePath+key;

    if((alarmSocket = socket(AF_UNIX,SOCK_STREAM,0)) == -1)
    {
		//LOG4CXX_ERROR(m_logger,"socket: " << strerror(errno));
    	// syslog takes 2+ arguments.
    	// 1st: where and what; OR together facility and level. note that
    	//      if you elect to use openlog, facilty can be left out.
    	// 2nd: String that will be logged
    	// 3rd: arguments for formatted string, much like printf
		syslog(LOG_ERR, "ERROR: socket: %s", strerror(errno));
		close(alarmSocket);
		exit(1);
    }
    remote.sun_family = AF_UNIX;

    strcpy(remote.sun_path, key.c_str());
    unlink(remote.sun_path);

    len = strlen(remote.sun_path) + sizeof(remote.sun_family);

    if(bind(alarmSocket,(struct sockaddr *)&remote,len) == -1)
    {
		//LOG4CXX_ERROR(m_logger,"bind: " << strerror(errno));
		syslog(LOG_ERR, "Line: %d ERROR: bind: %s", __LINE__, strerror(errno));
		close(alarmSocket);
        exit(1);
    }

    if(listen(alarmSocket, SOCKET_QUEUE_SIZE) == -1)
    {
		//LOG4CXX_ERROR(m_logger,"listen: " << strerror(errno));
		syslog(LOG_ERR, "Line: %d ERROR: listen: %s", __LINE__, strerror(errno));
		close(alarmSocket);
        exit(1);
    }

	//"Main Loop"
	while(true)
	{
		ReceiveAlarm();

		if(suspect == NULL)
		{
			continue;
		}

		//If this is from us, then ignore it!
		if((hostAddr.sin_addr.s_addr == suspect->IP_address.s_addr)
				||(hostAddr.sin_addr.s_addr == loopbackAddr.sin_addr.s_addr))
		{
			delete suspect;
			suspect = NULL;
			continue;
		}

		//cout << "Alarm!\n";
		//cout << suspect->ToString();
		//Keep track of what suspects we've swapped and their current state
		// Otherwise rules can accumulate in IPtables. Then when you go to delete one,
		// there will still be more left over and it won't seem to have worked.



		pthread_rwlock_rdlock(&lock);
		//If the suspect already exists in our table
		if(SuspectTable.find(suspect->IP_address.s_addr) != SuspectTable.end())
		{
			//If hostility hasn't changed
			if(SuspectTable[suspect->IP_address.s_addr] == suspect->isHostile)
			{
				//Do nothing. This means no change has happened since last alarm
				delete suspect;
				suspect = NULL;
				pthread_rwlock_unlock(&lock);
				continue;
			}
		}

		pthread_rwlock_unlock(&lock);
		pthread_rwlock_wrlock(&lock);

		SuspectTable[suspect->IP_address.s_addr] = suspect->isHostile;

		pthread_rwlock_unlock(&lock);

		if(suspect->isHostile && isEnabled)
		{
			inet_ntop(AF_INET, &(suspect->IP_address), suspectAddr, INET_ADDRSTRLEN);

			commandLine = "sudo iptables -t nat -A PREROUTING -d ";
			commandLine += hostAddrString;
			commandLine += " -s ";
			commandLine += suspectAddr;
			commandLine += " -j DNAT --to-destination ";
			commandLine += doppelgangerAddrString;

			system(commandLine.c_str());
		}
		else
		{
			inet_ntop(AF_INET, &(suspect->IP_address), suspectAddr, INET_ADDRSTRLEN);

			commandLine = "sudo iptables -t nat -D PREROUTING -d ";
			commandLine += hostAddrString;
			commandLine += " -s ";
			commandLine += suspectAddr;
			commandLine += " -j DNAT --to-destination ";
			commandLine += doppelgangerAddrString;

			system(commandLine.c_str());
		}
		delete suspect;
		suspect = NULL;
	}
}

void *Nova::DoppelgangerModule::GUILoop(void *ptr)
{
	if((IPCsock = socket(AF_UNIX,SOCK_STREAM,0)) == -1)
	{
		//LOG4CXX_ERROR(m_logger, "socket: " << strerror(errno));
		syslog(LOG_ERR, "Line: %d ERROR: socket: %s", __LINE__, strerror(errno));
		close(IPCsock);
		exit(1);
	}

	localIPCAddress.sun_family = AF_UNIX;

	//Builds the key path
	string key = homePath + DM_GUI_FILENAME;

	strcpy(localIPCAddress.sun_path, key.c_str());
	unlink(localIPCAddress.sun_path);

	int GUILen = strlen(localIPCAddress.sun_path) + sizeof(localIPCAddress.sun_family);

	if(bind(IPCsock,localIPCAddressPtr,GUILen) == -1)
	{
		//LOG4CXX_ERROR(m_logger, "bind: " << strerror(errno));
		syslog(LOG_ERR, "Line %d ERROR: bind: %s", __LINE__, strerror(errno));
		close(IPCsock);
		exit(1);
	}

	if(listen(IPCsock, SOCKET_QUEUE_SIZE) == -1)
	{
		//LOG4CXX_ERROR(m_logger, "listen: " << strerror(errno));
		syslog(LOG_ERR, "Line: %d ERROR: listen: %s", __LINE__, strerror(errno));
		close(IPCsock);
		exit(1);
	}
	while(true)
	{
		ReceiveGUICommand();
	}
}

void DoppelgangerModule::ReceiveGUICommand()
{
	struct sockaddr_un msgRemote;
    int socketSize, msgSocket;
    int bytesRead;
    GUIMsg msg = GUIMsg();
    u_char msgBuffer[MAX_GUIMSG_SIZE];

    socketSize = sizeof(msgRemote);
    //Blocking call
    if ((msgSocket = accept(IPCsock, (struct sockaddr *)&msgRemote, (socklen_t*)&socketSize)) == -1)
    {
		//LOG4CXX_ERROR(m_logger,"accept: " << strerror(errno));
		syslog(LOG_ERR, "Line: %d ERROR: accept: %s", __LINE__, strerror(errno));
		close(msgSocket);
    }
    if((bytesRead = recv(msgSocket, msgBuffer, MAX_GUIMSG_SIZE, 0 )) == -1)
    {
		//LOG4CXX_ERROR(m_logger,"recv: " << strerror(errno));
		syslog(LOG_ERR, "Line: %d ERROR: recv: %s", __LINE__, strerror(errno));
		close(msgSocket);
    }

    msg.DeserializeMessage(msgBuffer);
    switch(msg.GetType())
    {
    	case EXIT:
    		system("sudo iptables -F");
    		exit(1);
    	case CLEAR_ALL:
    		pthread_rwlock_wrlock(&lock);
			SuspectTable.clear();
			pthread_rwlock_unlock(&lock);
			break;
    	case CLEAR_SUSPECT:
    		//TODO still no functionality for this yet
			break;
    	default:
    		break;
    }
    close(msgSocket);
}


void Nova::DoppelgangerModule::ReceiveAlarm()
{
    //Blocking call
    if ((connectionSocket = accept(alarmSocket, remoteAddrPtr, socketSizePtr)) == -1)
    {
		//LOG4CXX_ERROR(m_logger,"accept: " << strerror(errno));
		syslog(LOG_ERR, "Line: %d ERROR: accept: %s", __LINE__, strerror(errno));
		close(connectionSocket);
        return;
    }
    if((bytesRead = recv(connectionSocket, buf, MAX_MSG_SIZE, 0 )) == -1)
    {
    	//LOG4CXX_ERROR(m_logger,"recv: " << strerror(errno));
    	syslog(LOG_ERR, "Line: %d ERROR: recv: %s", __LINE__, strerror(errno));
		close(connectionSocket);
        return;
    }
	suspect = new Suspect();
	try
	{
		suspect->DeserializeSuspect(buf);
		bzero(buf, bytesRead);
	}
	catch(std::exception e)
	{
		//LOG4CXX_ERROR(m_logger, "Error interpreting received Silent Alarm: " << string(e.what()));
		syslog(LOG_ERR, "Line: %d ERROR: Error interpreting received Silent Alarm: %s", __LINE__, string(e.what()).c_str());
		delete suspect;
		suspect = NULL;
	}
	close(connectionSocket);
	return;
}


string Nova::DoppelgangerModule::Usage()
{
	string usage_tips = "Nova Doppelganger Module\n";
	usage_tips += "\tUsage: DoppelgangerModule -l <log config file> -n <NOVA config file>\n";
	usage_tips += "\t-l: Path to LOG4CXX config xml file.\n";
	usage_tips += "\t-n: Path to NOVA config txt file.\n";

	return usage_tips;
}


void DoppelgangerModule::LoadConfig(char* configFilePath)
{
	string prefix;
	bool v = true;

	NOVAConfiguration * NovaConfig = new NOVAConfiguration();
	NovaConfig->LoadConfig(configFilePath, homePath);

	const string prefixes[] = {"INTERFACE", "DM_HONEYD_CONFIG",
			"DOPPELGANGER_IP", "DM_ENABLED", "USE_TERMINALS", "SILENT_ALARM_PORT"};

	openlog("DoppelgangerModule", LOG_CONS | LOG_PID | LOG_NDELAY | LOG_PERROR, LOG_LOCAL0);

	for (uint i = 0; i < sizeof(prefixes)/sizeof(prefixes[0]); i++)
	{
		prefix = prefixes[i];

		NovaConfig->options[prefix];
		if (!NovaConfig->options[prefix].isValid)
		{
			syslog(LOG_ERR, "Line: %d ERROR: The configuration variable # %s was not set in configuration file %s", __LINE__, prefixes[i].c_str(), configFilePath);
			v = false;
		}
	}

	//Checks to make sure all values have been set.
	if(v == false)
	{
		syslog(LOG_ERR, "Line: %d ERROR: One or more values have not been set in configuration file %s", __LINE__, configFilePath);
		exit(1);
	}
	else
	{
		syslog(LOG_INFO, "INFO: Config loaded successfully.");
	}

	hostAddrString = GetLocalIP(NovaConfig->options["INTERFACE"].data.c_str());
	if(hostAddrString.size() == 0)
	{
		//LOG4CXX_ERROR(m_logger, "Bad interface, no IP's associated!");
		syslog(LOG_ERR, "Line: %d ERROR: Bad interface, no IP's associated", __LINE__);
		exit(1);
	}

	inet_pton(AF_INET,hostAddrString.c_str(),&(hostAddr.sin_addr));

	doppelgangerAddrString = NovaConfig->options["DOPPELGANGER_IP"].data;
	struct in_addr *tempr = NULL;

	if( inet_aton(doppelgangerAddrString.c_str(), tempr) == 0)
	{
		//LOG4CXX_ERROR(m_logger,"Invalid doppelganger IP address!");
		syslog(LOG_ERR, "Line: %d ERROR: Invalid doppelganger IP address", __LINE__);
		exit(1);
	}

	useTerminals = atoi(NovaConfig->options["USE_TERMINALS"].data.c_str());
	sAlarmPort = atoi(NovaConfig->options["SILENT_ALARM_PORT"].data.c_str());
	isEnabled = atoi(NovaConfig->options["DM_ENABLED"].data.c_str());

	closelog();
}
