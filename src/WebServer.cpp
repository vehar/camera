
#include <Camera.h>

WebServer::WebServer(const std::string WebRoot)
{
	m_WebRoot = WebRoot;
	m_enabled = true;
	m_port = 8080;
	m_pid = -1;
	m_running = false;
}

WebServer::~WebServer()
{
	Stop();
}

bool WebServer::ConfigLoad(Json::Value &json)
{
	ScopedLock lock = ScopedLock(&m_mutex);
	m_props.clear();
	
	if (json.isMember("enabled") && json["enabled"].isBool())
		SetEnabled(json["enabled"].asBool());
	
	if (json.isMember("port") && json["port"].isNumeric())
	{
		if (SetPort(json["port"].asInt()) < 0)
		{
			LogError("WebServer::ConfigLoad - Failed to set port");
			return false;
		}
	}
	
	if (json.isMember("props"))
	{
		Json::Value &props = json["props"];
		for(unsigned int idx = 0; idx < props.size(); idx++)
		{
			std::string key = props[idx].asString();
			std::string value = props[key].asString();
			m_props[key] = value;
		}

	}
	
	return true;
}

bool WebServer::ConfigSave(Json::Value &json)
{
	ScopedLock lock = ScopedLock(&m_mutex);
	LogDebug("WebServer::ConfigSave");

	json["enabled"] = GetEnabled();
	json["port"] = GetPort();
	
	for(std::map<std::string, std::string>::iterator it = m_props.begin(); it != m_props.end(); it++)
	{
		json["props"][it->first] = it->second;
	}

	
	return true;
}
		
void WebServer::Start()
{
	ScopedLock lock = ScopedLock(&m_mutex);
	if (m_enabled == false)
		return;

	if (Exec() == false)
	{
		LogError("WebServer::Start - Failed. Won't try again!");
		return;
	}
	
	//Parent Prcoess - Start monitoring thread
	LogInfo("WebServer process has been started with pid %d", m_pid);
	m_running = true;
	Thread::Start();
	return;
}

void WebServer::Stop()
{
	ScopedLock lock = ScopedLock(&m_mutex);
	if (m_pid < 0)
		return; //No server running
	LogInfo("WebServer Stopping");
	if (kill(m_pid, 9) < 0)
	{
		LogError("WebServer::Stop() - Kill failed '%s'", strerror(errno));
	}
	m_running = false;
	lock.Unlock(); //We must unlock or we will deadlock waiting forever for the thread to exit. This probably has a small race it in
	Thread::Stop(); //Accept that m_pid is set to -1
}

void WebServer::Restart()
{
	ScopedLock lock = ScopedLock(&m_mutex);
	//Let Auto restart take care of starting it again
	if (m_pid < 0)
	{
		LogError("WebServer::Restart - Refusing restart because pid %d", m_pid);
		return;
	}
	LogInfo("WebServer::Restart - Restarting WebServer");
	if (kill(m_pid, 9) < 0)
	{
		LogError("WebServer::Stop() - Kill failed '%s'", strerror(errno));
		abort();
	}
}

void WebServer::SetEnabled(bool enabled)
{
	ScopedLock lock = ScopedLock(&m_mutex);
	
	if (m_enabled == false)
	{
		if (enabled == false)
			return; //Already stopped
		//Start server
		m_enabled = enabled;
		if (Exec() == false)
		{
			LogError("WebServer::Start - Failed. Won't try again!");
			return;
		}
	
		//Parent Prcoess - Start monitoring thread
		LogInfo("WebServer process has been started with pid %d", m_pid);
		m_running = true;
		Thread::Start();
	}
	else
	{
		if (enabled == true)
			return; //Already Started
		//Stop Server
		m_enabled = enabled;
		LogInfo("WebServer Stopping");
		if (kill(m_pid, 9) < 0)
		{
			LogError("WebServer::Stop() - Kill failed '%s'", strerror(errno));
		}
		m_running = false;
		lock.Unlock(); //We must unlock or we will deadlock waiting forever for the thread to exit. This probably has a small race it in
		Thread::Stop(); //Accept that m_pid is set to -1
	}
}

bool WebServer::GetEnabled()
{
	ScopedLock lock = ScopedLock(&m_mutex);
	return m_enabled;
}
		
int WebServer::SetPort(int port)
{
	if (port <= 0 || port >= 65535)
	{
		LogError("WebServer::SetPort(%d) - Invalid Port", port);
		return -ERANGE;
	}
	LogInfo("WebServer::SetPort(%d)", port);

	if (port == m_port)
	{
		LogDebug("WebServer::SetPort Ignored because new port is same as old port");
		return port;
	}

	ScopedLock lock = ScopedLock(&m_mutex);
	m_port = port;
	if (m_enabled)
		Restart();
	return port;
}

int WebServer::GetPort()
{
	ScopedLock lock = ScopedLock(&m_mutex);
	return m_port;
}

std::string WebServer::GetProperty(const std::string key)
{
	ScopedLock lock = ScopedLock(&m_mutex);
	std::map<std::string, std::string>::iterator it = m_props.find(key);
	if (it == m_props.end())
		return "";
	return it->second;
}

std::string WebServer::GetProperty(const std::string key, const std::string def)
{
	ScopedLock lock = ScopedLock(&m_mutex);
	std::map<std::string, std::string>::iterator it = m_props.find(key);
	if (it == m_props.end())
		return def;
	return it->second;
}

void WebServer::SetProperty(const std::string key, const std::string value)
{
	ScopedLock lock = ScopedLock(&m_mutex);
	m_props[key] = value;
}

bool WebServer::Exec()
{
	LogInfo("WebServer Starting On Port %d", m_port);
	if (m_pid >= 0)
	{
		LogCritical("WebServer::Start() - WebServer already started? aborting....");
		abort(); //Invalid state tried to start server twice!
	}

	//Build string for port for argument passing
	std::stringstream ss;
	ss << m_port;
	std::string sport = ss.str();
	LogInfo("WebRoot: %s", m_WebRoot.c_str());
	pid_t pid = fork();
	if (pid < 0)
	{
		LogError("WebServer::Start() - fork() failed '%s'", strerror(errno));
		return false;
	}

	if (pid == 0)
	{
		//FIXME: Hardcoded values
		
		//Close All fd's
		for(int i=3;i<1024;i++)
			close(i);
		
		//Restore all signals or process might freak's out!
		sigset_t all;
		if (sigfillset(&all) < 0)
			abort();

		if (sigprocmask(SIG_UNBLOCK, &all, NULL) < 0)
			abort();

		char port[128];
		snprintf(port, sizeof(port), "%d", m_port);

		if (setenv("PORT", port, 1) < 0)
		{
			perror("setenv");
			exit(EXIT_FAILURE);
		}

		if (chdir(m_WebRoot.c_str()) < 0)
		{
			perror("chdir");
			exit(EXIT_FAILURE);
		}

		if (execl("/usr/local/bin/nodemon", "/usr/local/bin/nodemon", "index.js", NULL) < 0)
		{
			printf("execl failed: %s\n", strerror(errno));
			abort();
		}

	}
	m_pid = pid;
	LogInfo("WebServer has new pid %d", m_pid);
	return true;
}

void WebServer::Run()
{
	int status = 0;
	int FastFailures = 0;
	struct timespec FastTime = {5, 0}; //Detect Fast failures
	struct timespec Started = {0, 0};

	while(1)
	{
		Time::MonoTonic(&Started);
		if (m_pid >= 0)
		{
			pid_t ret = waitpid(m_pid, &status, 0);
			if (ret < 0)
			{
				switch(errno)
				{
					case EINTR:
						continue;
						break;
					case ECHILD:
						LogError("WebServer::Run() - waitpid failed '%s'", strerror(errno));
						return;
						break;
					case EINVAL:
						abort();
						break;
				}
				LogError("WebServer::Run() - waitpid failed '%s'", strerror(errno));
				continue;
			}
		}
		ScopedLock lock = ScopedLock(&m_mutex);
		m_pid = -1;
		LogWarning("WebServer::Run() - WebServer exited");
		if (m_running == false)
		{
			LogDebug("WebServer::Run() - Monitor thread exiting");
			return;
		}

		struct timespec Now;
		struct timespec Diff;
		Time::MonoTonic(&Now);
		Time::Sub(&Now, &Started, &Diff);
		if (Time::IsLess(&Diff, &FastTime))
		{
			LogWarning("WebServer::Run() - Fast Failure Detected");
			FastFailures++;

			//FIXME: this is an ugly hack it could be better
			//basically if we crash / core the mono webserver will stay running
			//when we restart this prevents mono binding on the port again.
			//so we killall mono to fix this when we detect it happening.
			//so by "fixme" I mean we need to scan for the correct process to kill
			if (FastFailures > 5)
			{
				LogNotice("WebServer::Run() - Running killall nodemon");
				int ret = system("killall nodemon");
				LogDebug("WebServer::Run() - Exit Status WEXITSTATUS(%d)", WEXITSTATUS(ret));
				if (WEXITSTATUS(ret) != 0)
				{
					LogError("WebServer::Run() - Exit status is WEXITSTATUS(%d)", ret);
				}
				FastFailures = 0;
			}
		}
		else
		{
			if (FastFailures > 0)
			{
				LogDebug("WebServer::Run() - Reset FastFailure Counter");
				FastFailures = 0;
			}
		}
		
		LogInfo("WebServer::Run() - Restarting WebServer");
		if (Exec() == false)
		{
			LogError("WebServer::Run - Failed to start web server");
			lock.Unlock();
			sleep(1);
		}
	}
}


