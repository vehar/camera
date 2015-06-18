
class CameraHandler
{
	public:
		CameraHandler();
		~CameraHandler();

		void Init(const std::string Platform, const std::string CfgFile);
		bool ConfigLoad(Json::Value &);
		bool ConfigSave(Json::Value &);

		void VideoStreamCount(int *count);

		bool VideoStreamSetEnabled(unsigned int stream, bool enabled);
		bool VideoStreamGetEnabled(unsigned int stream, bool &enabled);

		bool VideoStreamEnable(unsigned int stream);
		bool VideoStreamDisable(unsigned int stream);

		void Wait();
		void Quit();


		WebServer *WServer;
		RTSPServer *RServer;
		Config *Cfg;

	private:
		Barrier m_QuitBarrier;

		PlatformBase *m_Platform;
	
		//Config Stuff
		std::string m_CfgFile;
		Mutex m_ConfigMutex;

		//Video Stuff
		Mutex m_VideoMutex;
		std::map<unsigned int, struct VideoStreamConfig *> m_VideoStreams;

};


