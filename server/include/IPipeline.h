
class IPipeline
{
	public:
		virtual ~IPipeline() { };

		virtual void Start() = 0;
		virtual void stop() = 0;
};

