
#include <Camera.h>
#include <dlfcn.h>

CameraHandler::CameraHandler()
{
	LogDebug("CameraHandler::CameraHandler");
	gst_init (0, NULL);
	
	m_gstlogger = new GstLogger();

	guint major, minor, micro, nano;
	gst_version(&major, &minor, &micro, &nano);
	LogInfo("Gstreamer Version: %u.%u.%u.%u", major, minor, micro, nano);

	WServer = NULL;
	WStream = NULL;
	RServer = NULL;
	m_dll = NULL;

}

CameraHandler::~CameraHandler()
{
	LogDebug("CameraHandler::~CameraHandler");

	//Cleanup Video Inputs's
	while(m_VideoInputs.size() > 0)
	{
		std::map<unsigned int, struct VideoInputConfig *>::iterator it = m_VideoInputs.begin();
		int input = it->first;
		VideoInputConfig *config = it->second;
		if (config->GetEnabled())
			m_Platform->VideoInputDisable(input);
		m_VideoInputs.erase(it);
		delete config;
	}
	
	//Cleanup Any timers we might have
	while(m_GPIOOutputTimers.size() > 0)
	{
		std::map<unsigned int, GPIOOutputTimer *>::iterator it = m_GPIOOutputTimers.begin();
		GPIOOutputTimer *t = it->second;
		CameraTimers->Remove(t);
		m_GPIOOutputTimers.erase(it);
		delete t;
	}

	//Cleanup any Video Output's
	while(m_VideoOutputs.size() > 0)
	{
		std::map<unsigned int, VideoOutputConfig *>::iterator it = m_VideoOutputs.begin();
//		int output = it->first;
		VideoOutputConfig *config = it->second;
//		if (config->GetEnabled())
//			m_Platform->VideoOutputDisable(output);
		m_VideoOutputs.erase(it);
		delete config;
	}

	//Cleanup any tours we may have
	while(m_VideoOutputTours.size() > 0)
	{
		std::map<std::string, VideoOutputTour *>::iterator it = m_VideoOutputTours.begin();
		VideoOutputTour *config = it->second;
		m_VideoOutputTours.erase(it);
		delete config;
	}

	WServer->Stop();
	delete WServer;
	
	delete WStream;

	User::Destroy();

	delete Cfg;
	delete m_Platform;
	delete RServer;
	
	if (m_dll)
		dlclose(m_dll);
	
	delete m_gstlogger;
	gst_deinit();
}

static bool CheckDLL(const std::string *path)
{
	struct stat info;
	LogDebug("Checking DLL '%s'", path->c_str());
	if (stat(path->c_str(), &info) == 0)
		return true;
	return false;
}

static bool FindDLL(const std::string &Platform, std::string *path)
{
	*path = Platform;
	if (CheckDLL(path))
		return true;
	
	*path = "lib" + Platform + ".so";
	if (CheckDLL(path))
		return true;
		
	*path = "camera-1.0/lib" + Platform + ".so";
	if (CheckDLL(path))
		return true;

	*path = "/usr/lib/camera-1.0/lib" + Platform + ".so";
	if (CheckDLL(path))
		return true;

	*path = "/usr/local/lib/camera-1.0/lib" + Platform + ".so";
	if (CheckDLL(path))
		return true;

	/* FIXME: This needs removed and replace with proper path + env searchs */		
	*path = "./platforms/example/.libs/lib" + Platform + ".so";
	if (CheckDLL(path))
		return true;		

	return false;	
}

//Init Does not need locking as it should be the only code active in the system during startup
void CameraHandler::Init(const std::string WebRoot, const std::string Platform, const std::string CfgFile)
{
	LogInfo("Version: %s", Version::ToString().c_str());
	LogDebug("CameraHandler::Init(\"%s\", \"%s\")", Platform.c_str(), CfgFile.c_str());
	m_CfgFile = CfgFile;

	//Start Various Services
	WServer = new WebServer(WebRoot);
	WStream = new WebStream();
	RServer = new RTSPServer();

	//Search for platform dll
	std::string dll = "";
	if (FindDLL(Platform, &dll) == false)
	{
		if (FindDLL(String::ToLower(Platform), &dll) == false)
		{
			LogCritical("Cannot find dll for platform '%s'", Platform.c_str());
			exit(EXIT_FAILURE);
		}
	}

	m_dll = dlopen(dll.c_str(), RTLD_NOW);
	if (m_dll == NULL)
	{
		LogCritical("Fail to load dll: %s", dll.c_str());
		LogCritical("Error: %s", dlerror());
		exit(EXIT_FAILURE);
	}
	
	PlatformBase *(*create)() = (PlatformBase *(*)()) dlsym(m_dll, "Create");
	if (create == NULL)
	{
		LogCritical("DLL '%s' does not contain a symbol for 'Create'", dll.c_str());
		exit(EXIT_FAILURE);
	}
	

	m_Platform = create();
	if (m_Platform == NULL)
	{
		LogCritical("Fail to load platform: %s", Platform.c_str());
		exit(EXIT_FAILURE);
	}

	if (m_Platform->Init() == false)
	{
		LogCritical("Failed To Init Platform exiting....");
		exit(EXIT_FAILURE);
	}

	if (User::Init() == false)
	{
		LogCritical("Failed to Init user database....");
		exit(EXIT_FAILURE);
	}

	//Dump out some debug info about how many streams we support
	unsigned int nInputs = m_Platform->VideoInputCount();
	LogInfo("Supported Video Inputs: %d", nInputs);
	for(unsigned int i = 0; i < nInputs; i++)
	{
		VideoInputSupported info;
		info.Clear();
		if (m_Platform->VideoInputSupportedInfo(i, &info) == false)
		{
			LogError("Failure to VideoInputSupportedInfo(%d)", i);
			exit(EXIT_FAILURE);
		}
		LogInfo("VideoInput %u Supports", i);

		std::vector<std::string> lst = info.ToStrV();
		for(std::vector<std::string>::iterator it = lst.begin(); it != lst.end(); it++)
		{
			std::string tmp = *it;
			LogInfo("%s", tmp.c_str());
		}
	}
	
	//Video Outputs
	LogInfo("Supported Video Outputs: %u", m_Platform->VideoOutputCount());

	//Audio Inputs
	LogInfo("Supported Audio Inputs: %u", m_Platform->AudioInputCount());

	//Audio Outputs
	LogInfo("Supported Audio Outputs: %u", m_Platform->AudioOutputCount());
	
	//GPIO Inputs
	LogInfo("Supported GPIO Inputs: %u", m_Platform->GPIOInputCount());

	//GPIO Outputs
	LogInfo("Supported GPIO Outputs: %u", m_Platform->GPIOOutputCount());

	//Load Default Config's. The Config Load can override these later
	//We also wait until after the config load until we enable the config + enable the streams on the platform.
	for(unsigned int i = 0; i < nInputs; i++)
	{
		//Load Up Default Configs
		VideoInputConfig *config = new VideoInputConfig();
		m_Platform->VideoInputDefaultConfig(i, config);
		LogInfo("VideoInput %u default Config: '%s'", i, config->ToStr().c_str());
		m_VideoInputs[i] = config;
	}

	//Load The actual configuration
	Cfg = new Config(this, CfgFile);

	if (Cfg->Load() == false)
	{
		LogCritical("Failed To Load Config File: \"%s\" exiting ....", m_CfgFile.c_str());
		exit(EXIT_FAILURE);
	}


	//Start The Video Inputs
	std::map<unsigned int, VideoInputConfig *>::iterator it = m_VideoInputs.begin();
	while(it != m_VideoInputs.end())
	{
		if (m_Platform->VideoInputConfigure(it->first, it->second) == false)
		{
			LogError("CameraHandler::Init Failed to Configure Input %u Config: %s", it->first, it->second->ToStr().c_str());
			abort();
		}
		if (it->second->GetEnabled())
		{
			if (VideoInputEnable(it->first) == false)
			{
				LogError("CameraHandler::Init Failed to Enable VideoInput %u", it->first);
				abort();
			}
		}
		it++;
	}
	
	//Start the VideoOutputs
	//TODO
	
	//Load WebService Last
	WServer->Start();
}

bool CameraHandler::ConfigLoad(Json::Value &json)
{
	LogDebug("CameraHandler::ConfigLoad");
	ScopedLock Lock = ScopedLock(&m_ConfigMutex);

	if (json.isMember("debug"))
		if (Debug::ConfigLoad(json["debug"]) == false)
			return false;

	if (json.isMember("users"))
		if (User::ConfigLoad(json["users"]) == false)
			return false;

	if (json.isMember("group"))
		if (Group::ConfigLoad(json["groups"]) == false)
			return false;

	if (json.isMember("platform"))
		if (m_Platform->ConfigSave(json["platform"]) == false)
			return false;

	if (json.isMember("rtspserver"))
		if (RServer->ConfigLoad(json["rtspserver"]) == false)
			return false;
			
	if (json.isMember("webserver"))
		if (WServer->ConfigLoad(json["webserver"]) == false)
			return false;

	{
		ScopedLock VideoInputLock(&m_VideoInputMutex);
		std::map<unsigned int, VideoInputConfig *>::iterator it = m_VideoInputs.begin();
		while(it != m_VideoInputs.end())
		{
			std::stringstream ss;
			ss << "VideoInputConfig_" << it->first;
			if (json.isMember(ss.str()))
			{
				if (it->second->ConfigLoad(json[ss.str()]) == false)
				{
					LogWarning("CameraHandler::ConfigLoad - Failed to load configuration for video input '%s'", ss.str().c_str());
					return false;
				}
				LogInfo("VideoInput %u loaded config '%s'", it->first, it->second->ToStr().c_str());
			}
			else
			{
				LogWarning("CameraHandler::ConfigLoad - No configuration for video input '%s'", ss.str().c_str());
			}
			it++;
		}
	}

	{
		ScopedLock VideoOutputLock(&m_VideoOutputMutex);
		std::map<unsigned int, VideoOutputConfig *>::iterator it = m_VideoOutputs.begin();
		while(it != m_VideoOutputs.end())
		{
			std::stringstream ss;
			ss << "VideoutOutputConfig_" << it->first;
			if (json.isMember(ss.str()))
			{
				if (it->second->ConfigLoad(json[ss.str()]) == false)
				{
					LogWarning("CameraHandler::ConfigLoad - Failed to load configuration for video output '%s'", ss.str().c_str());
					return false;
				}
				LogInfo("VideoOutput %u loaded config '%s'", it->first, it->second->ToStr().c_str());
			}
			else
			{
				LogWarning("CameraHandler::ConfigLoad - No configuration for video output '%s'", ss.str().c_str());
			}
			it++;
		}
	}

	//Load VideoOutput Tours
	{
		ScopedLock VideoOutputLock(&m_VideoOutputMutex);
		if (json.isMember("VideoOutputTours") && json["VideoOutputTours"].isArray())
		{
			std::vector<std::string> lst = json["VideoOutputTours"].getMemberNames();
			for(std::vector<std::string>::iterator it = lst.begin(); it != lst.end(); it++)
			{
				std::string name = *it;
				
				VideoOutputTour *tour = new VideoOutputTour();
				if (tour->ConfigLoad(json["VideoOutputTours"][name]) == false)
				{
					LogError("CameraHandler::ConfigLoad - Failed to load tour '%s'", name.c_str());
					delete tour;
					return false;
				}
				m_VideoOutputTours[tour->GetName()] = tour;
				LogInfo("CameraHandler::ConfigLoad - Loaded Tour '%s'", tour->GetName().c_str());
			}
		}
	}
	

	return true;
}

bool CameraHandler::ConfigSave(Json::Value &json)
{
	LogDebug("CameraHandler::ConfigSave");
	ScopedLock Lock = ScopedLock(&m_ConfigMutex);

	json["Version"] = Version::ToString();

	if (Debug::ConfigSave(json["debug"]) == false)
		return false;

	if (m_Platform->ConfigSave(json["platform"]) == false)
		return false;

	if (RServer->ConfigSave(json["rtspserver"]) == false)
		return false;

	//Save VideoInput Config
	{
		ScopedLock VideoInputLock = ScopedLock(&m_VideoInputMutex);
		std::map<unsigned int, VideoInputConfig *>::iterator it = m_VideoInputs.begin();
		while(it != m_VideoInputs.end())
		{
			std::stringstream ss;
			ss << "VideoInputConfig_" << it->first;
			if (it->second->ConfigSave(json[ss.str()]) == false)
			{
				LogError("CameraHandler::ConfigSave Failed to save configuration for video input '%s'", ss.str().c_str());
				return false;
			}
			it++;
		}
	}

	//Save VideoOutput Config
	{
		ScopedLock VideoOutputLock = ScopedLock(&m_VideoOutputMutex);
		std::map<unsigned int, VideoOutputConfig *>::iterator it = m_VideoOutputs.begin();
		while(it != m_VideoOutputs.end())
		{
			std::stringstream ss;
			ss << "VideoOutputConfig_" << it->first;
			if (it->second->ConfigSave(json[ss.str()]) == false)
			{
				LogError("CameraHandler::ConfigSave Failed to save configuration for video output '%s'", ss.str().c_str());
				return false;
			}
			it++;
		}
	}

	//Save VideoOutputTours
	{
		ScopedLock VideoOutputLock = ScopedLock(&m_VideoOutputMutex);
		std::map<std::string, VideoOutputTour *>::iterator it = m_VideoOutputTours.begin();
		json["VideoOutputTours"] = Json::arrayValue;
		while(it != m_VideoOutputTours.end())
		{
			Json::Value sub;
			if (it->second->ConfigSave(sub) == false)
			{
				LogError("CameraHandler::ConfigSave Failed to save configuration for tour '%s'", it->second->GetName().c_str());
				return false;
			}
			json["VideoOutputTours"].append(sub);
			it++;
		}
	}

	if (User::ConfigSave(json["users"]) == false)
		return false;

	if (Group::ConfigSave(json["groups"]) == false)
		return false;

	if (WServer->ConfigSave(json["webserver"]) == false)
		return false;

	return true;
}

int CameraHandler::VideoInputCount()
{
	ScopedLock VideoLock = ScopedLock(&m_VideoInputMutex);
	LogDebug("CameraHandler::VideoInputCount");
	return m_Platform->VideoInputCount();
}

bool CameraHandler::VideoInputSetEnabled(unsigned int input, bool enabled)
{
	ScopedLock VideoLock = ScopedLock(&m_VideoInputMutex);
	LogDebug("CameraHandler::VideoInputSetEnabled(%u, %s)", input, enabled ? "True" : "False");
	std::map<unsigned int, VideoInputConfig *>::iterator it = m_VideoInputs.find(input);
	if (it == m_VideoInputs.end())
	{
		LogDebug("CameraHandler::VideoInputSetEnabled(%u, %s) - No such input", input, enabled ? "True" : "False");
		return false;
	}


	bool orig = m_VideoInputs[input]->GetEnabled();

	if (orig == false && enabled == true)
	{
		m_VideoInputs[input]->SetEnabled(true);
		return VideoInputEnable(input);
	}

	if (orig == true && enabled == false)
	{
		m_VideoInputs[input]->SetEnabled(false);
		return VideoInputDisable(input);
	}

	LogError("CameraHandler::VideoInputSetEnabled() - Did Nothing - Probably a bug in the client...");
	return true;
}

bool CameraHandler::VideoInputGetEnabled(unsigned int input, bool &enabled)
{
	ScopedLock VideoLock = ScopedLock(&m_VideoInputMutex);
	LogDebug("CameraHandler::VideoStreamGetEnabled(%u)", input);
	std::map<unsigned int, VideoInputConfig *>::iterator it = m_VideoInputs.find(input);
	if (it == m_VideoInputs.end())
	{
		LogError("CameraHandler::VideoInputGetEnabled(%u) - No such input", input);
		return false;
	}

	enabled = m_VideoInputs[input]->GetEnabled();
	return true;
}

int CameraHandler::VideoInputSetConfig(unsigned int input, VideoInputConfig *cfg)
{
	ScopedLock VideoLock = ScopedLock(&m_VideoInputMutex);
	LogDebug("CameraHandler::VideoInputSetConfig(%u)", input);
	std::map<unsigned int, VideoInputConfig *>::iterator it = m_VideoInputs.find(input);
	if (it == m_VideoInputs.end())
	{
		LogError("CameraHandler::VideoInputSetConfig(%u) - No such input", input);
		return -EEXIST;
	}
	struct VideoInputConfig *oldcfg = it->second;
	if (oldcfg->GetEnabled())
	{
		if (VideoInputDisable(input) == false)
		{
			LogError("CameraHandler::VideoInputSetConfig(%u) - Failed to disable input", input);
			return -1;
		}
	}

	if (m_Platform->VideoInputConfigure(input, cfg) == false)
	{
		LogError("CameraHandler::VideoInputSetConfig(%u) - Failed to configure input", input);
		if (oldcfg->GetEnabled())
		{
			LogInfo("CameraHandler::VideoInputSetConfig(%u) - Restoring original config", input);
			if (VideoInputEnable(input) == false)
			{
				LogCritical("CameraHandler::VideoInputSetConfig - Cannot enable video input again this is FATAL");
				abort();
			}
		}
		return -1;
	}

	if (cfg->GetEnabled())
	{
		if (VideoInputEnable(input) == false)
		{
			LogCritical("CameraHandler::VideoInputSetConfig - Cannot enable video input again this is FATAL");
			abort();
		}
	}
	*oldcfg = *cfg;
	LogInfo("CameraHandler::VideoInputSetConfig(%u) - Now configured to be '%s'", input, cfg->ToStr().c_str());
	return 0;
}

int CameraHandler::VideoInputGetConfig(unsigned int input, VideoInputConfig *cfg)
{
	ScopedLock VideoLock = ScopedLock(&m_VideoInputMutex);
	LogDebug("CameraHandler::VideoInputGetConfig(%u)", input);
	std::map<unsigned int, VideoInputConfig *>::iterator it = m_VideoInputs.find(input);
	if (it == m_VideoInputs.end())
	{
		LogError("CameraHandler::VideoInputGetConfig(%u) - No such input", input);
		return -EEXIST;
	}
	struct VideoInputConfig *tmp = it->second;
	*cfg = *tmp;
	return 0;
}

int CameraHandler::VideoInputGetSupported(unsigned int input, VideoInputSupported *info)
{
	ScopedLock VideoLock = ScopedLock(&m_VideoInputMutex);
	LogDebug("CameraHandler::VideoInputGetSupported(%u)", input);
	std::map<unsigned int, VideoInputConfig *>::iterator it = m_VideoInputs.find(input);
	if (it == m_VideoInputs.end())
	{
		LogError("CameraHandler::VideoInputGetSupported(%u) - No such input", input);
		return -EEXIST;
	}
	if (m_Platform->VideoInputSupportedInfo(input, info) == false)
		return -1;
	return 0;
}

bool CameraHandler::VideoInputEnable(unsigned int input)
{
	ScopedLock VideoLock = ScopedLock(&m_VideoInputMutex);
	LogDebug("CameraHandler::VideoInputEnable(%u)", input);
	std::map<unsigned int, VideoInputConfig *>::iterator it = m_VideoInputs.find(input);
	if (it == m_VideoInputs.end())
	{
		LogDebug("CameraHandler::VideoInputEnable(%u) - No such input", input);
		return false;
	}

	std::stringstream url;
	url << "/video/" << input;

	if (m_Platform->VideoInputEnable(input) == false)
	{
		LogError("Platform Failed to enabled video input %u", input);
		return false;
	}

	std::stringstream pipe;
	if (m_VideoInputs[input]->GetCodec() == "H264")
		pipe << "( internalsrc streamname=video" << input << " ! h264parse ! rtph264pay name=pay0 pt=96 )";
	else if (m_VideoInputs[input]->GetCodec() == "H263")
		pipe << "( internalsrc streamname=video" << input << " ! rtph263pay name=pay0 pt=96 )";
	else if (m_VideoInputs[input]->GetCodec() == "MJPEG")
		pipe << "( internalsrc streamname=video" << input << " ! rtpjpegpay name=pay0 pt=96 )";
	else if (m_VideoInputs[input]->GetCodec() == "JP2K")
		pipe << "( internalsrc streamname=video" << input << " ! rtpj2kpay name=pay0 pt=96 )";
	else if (m_VideoInputs[input]->GetCodec() == "JP2K")
		pipe << "( internalsrc streamname=video" << input << " ! rtpmp4vpay name=pay0 pt=96 )";
	else if (m_VideoInputs[input]->GetCodec() == "THEORA")
		pipe << "( internalsrc streamname=video" << input << " ! rtptheorapay name=pay0 pt=96 )";
	else if (m_VideoInputs[input]->GetCodec() == "VP8")
		pipe << "( internalsrc streamname=video" << input << " ! rtpvp8pay name=pay0 pt=96 )";
	else
	{
		LogCritical("Unknown Codec: %s", m_VideoInputs[input]->GetCodec().c_str());
		abort();
	}
	RServer->PipelineAdd(url.str().c_str(), pipe.str().c_str());
	return true;
}

bool CameraHandler::VideoInputDisable(unsigned int input)
{
	ScopedLock VideoLock = ScopedLock(&m_VideoInputMutex);
	LogDebug("CameraHandler::VideoInputDisable(%u)", input);
	std::map<unsigned int, VideoInputConfig *>::iterator it = m_VideoInputs.find(input);
	if (it == m_VideoInputs.end())
	{
		LogDebug("CameraHandler::VideoInputDisable(%u) - No such input", input);
		return false;
	}

	std::stringstream url;
	url << "/video/" << input;

	RServer->PipelineRemove(url.str().c_str());
	if (m_Platform->VideoInputDisable(input) == false)
	{
		LogError("Platform Failed to disable video input %u", input);
		return false;
	}
	return true;
}

int CameraHandler::VideoOutputCount()
{
	ScopedLock VideoLock = ScopedLock(&m_VideoOutputMutex);
	LogDebug("CameraHandler::VideoOutputCount()");
	return m_Platform->VideoOutputCount();
}

int CameraHandler::VideoOutputGetSupported(unsigned int output, VideoOutputSupported *info)
{
	ScopedLock VideoLock = ScopedLock(&m_VideoOutputMutex);
	LogDebug("CameraHandler::VideoOutputGetSupported(%d, %p)", output, info);
	return -1;
}

std::vector<std::string> CameraHandler::VideoOutputTourList()
{
	ScopedLock VideoLock = ScopedLock(&m_VideoOutputMutex);
	LogDebug("CameraHandler::VideoOutputTourList()");
	std::vector<std::string> lst;
}

int CameraHandler::VideoOutputTourAdd(VideoOutputTour *tour)
{
	ScopedLock VideoLock = ScopedLock(&m_VideoOutputMutex);
	LogDebug("CameraHandler::VideoOutputTourAdd(%s)", tour->GetName().c_str());
	std::map<std::string, VideoOutputTour *>::iterator it = m_VideoOutputTours.find(tour->GetName());
	if (it != m_VideoOutputTours.end())
	{
		LogError("CameraHandler::VideoOutputTourAdd - Tour '%s' already exists", tour->GetName().c_str());
		return -EEXIST;
	}
	VideoOutputTour *tmp = new VideoOutputTour();
	*tmp = *tour;
	m_VideoOutputTours[tour->GetName()] = tmp;
}

int CameraHandler::VideoOutputTourUpdate(VideoOutputTour *tour)
{
	ScopedLock VideoLock = ScopedLock(&m_VideoOutputMutex);
	LogDebug("CameraHandler::VideoOutputTourList(%s)", tour->GetName().c_str());
	if (VideoOutputTourExists(tour->GetName()))
	{
		if (VideoOutputTourRemove(tour->GetName()) < 0)
		{
			LogCritical("CameraHandler::VideoOutputTourUpdate - Tour '%s' Exists but could not be removed?", tour->GetName().c_str());
			abort(); //Should be unreachable
		}
	}
	return VideoOutputTourAdd(tour);
}

int CameraHandler::VideoOutputTourGet(const std::string &name, VideoOutputTour *info)
{
	ScopedLock VideoLock = ScopedLock(&m_VideoOutputMutex);
	LogDebug("CameraHandler::VideoOutputTourGet(%s)", name.c_str());
	std::map<std::string, VideoOutputTour *>::iterator it = m_VideoOutputTours.find(name);
	if (it == m_VideoOutputTours.end())
	{
		LogError("CameraHandler::VideoOutputTourRemove - Tour '%s' does not exist", name.c_str());
		return -EEXIST;
	}
	*info = *it->second;
	return 0;
}

bool CameraHandler::VideoOutputTourExists(const std::string &name)
{
	ScopedLock VideoLock = ScopedLock(&m_VideoOutputMutex);
	LogDebug("CameraHandler::VideoOutputTourExists(%s)", name.c_str());
	std::map<std::string, VideoOutputTour *>::iterator it = m_VideoOutputTours.find(name);
	if (it == m_VideoOutputTours.end())
		return false;
	return true;
}

int CameraHandler::VideoOutputTourRemove(const std::string &name)
{
	ScopedLock VideoLock = ScopedLock(&m_VideoOutputMutex);
	LogDebug("CameraHandler::VideoOutputTourRemove(%s)", name.c_str());
	std::map<std::string, VideoOutputTour *>::iterator it = m_VideoOutputTours.find(name);
	if (it == m_VideoOutputTours.end())
	{
		LogError("CameraHandler::VideoOutputTourRemove - Tour '%s' does not exist", name.c_str());
		return -EEXIST;
	}
	delete it->second;
	m_VideoOutputTours.erase(it);
	return 0;
}

int CameraHandler::GPIOOutputCount()
{
	ScopedLock VideoLock = ScopedLock(&m_VideoInputMutex);
	LogDebug("CameraHandler::GPIOOutputCount");
	return m_Platform->GPIOOutputCount();
}

int CameraHandler::GPIOOutputSetState(unsigned int output, bool state)
{
	ScopedLock VideoLock = ScopedLock(&m_VideoInputMutex);
	LogDebug("CameraHandler::GPIOOutputSetState(%u, %s)", output, state ? "On" : "Off");
	if (output >= CameraHandler::GPIOOutputCount())
		return -EEXIST;

	//Cancel existing timers if they exist	
	std::map<unsigned int, GPIOOutputTimer *>::iterator it = m_GPIOOutputTimers.find(output);
	if (it != m_GPIOOutputTimers.end())
	{
		GPIOOutputTimer *t = it->second;
		CameraTimers->Remove(t);
		m_GPIOOutputTimers.erase(it);
		delete t;
	}

	m_Platform->GPIOOutputSetState(output, state);
}

int CameraHandler::GPIOOutputSetState(unsigned int output, bool state, const struct timespec *tv)
{
	ScopedLock VideoLock = ScopedLock(&m_VideoInputMutex);
	LogDebug("CameraHandler::GPIOOutputSetState(%u, %s, { %ld, %ld })", output, state ? "On" : "Off", tv->tv_sec, tv->tv_nsec);
	if (output >= CameraHandler::GPIOOutputCount())
		return -EEXIST;

	//Cancel existing timers if they exist	
	std::map<unsigned int, GPIOOutputTimer *>::iterator it = m_GPIOOutputTimers.find(output);
	if (it != m_GPIOOutputTimers.end())
	{
		GPIOOutputTimer *t = it->second;
		CameraTimers->Remove(t);
		m_GPIOOutputTimers.erase(it);
		delete t;
	}

	bool next = state ? false : true; //Invert

	m_Platform->GPIOOutputSetState(output, state);
	GPIOOutputTimer *t = new GPIOOutputTimer(output, this, tv, next);
	m_GPIOOutputTimers[output] = t;
	CameraTimers->Add(t);
	return 0;
}

bool CameraHandler::GPIOOutputGetState(unsigned int output)
{
	ScopedLock VideoLock = ScopedLock(&m_VideoInputMutex);
	LogDebug("CameraHandler::GPIOOutputGetState(%u)", output);
	return m_Platform->GPIOOutputGetState(output);
}

void CameraHandler::Wait()
{
	LogDebug("CameraHandler::Wait");
	LogInfo("System Running");
	m_QuitBarrier.Wait();
}

void CameraHandler::Quit()
{
	LogDebug("CameraHandler::Quit");
	Cfg->Dirty();
	m_QuitBarrier.WakeUp();
}


