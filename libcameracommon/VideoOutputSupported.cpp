
#include <stdlib.h>
#include <string>
#include <sstream>
#include <map>
#include <list>

#include <json/json.h>

#include <VideoOutputSupported.h>

VideoOutputSupported::VideoOutputSupported()
{
	Clear();
}

VideoOutputSupported::~VideoOutputSupported()
{

}

void VideoOutputSupported::SetMaxPanes(int value)
{
	m_maxpanes = value;
}

int VideoOutputSupported::GetMaxPanes()
{
	return m_maxpanes;
}

void VideoOutputSupported::Clear()
{
	m_maxpanes = 1; //Must always be at least 1
}

std::string VideoOutputSupported::Encode()
{
	Json::Value json;

	json["maxpanes"] = m_maxpanes;

	std::stringstream ss;
	Json::StyledWriter styledWriter;
	ss << styledWriter.write(json);
	return ss.str();
}

bool VideoOutputSupported::Decode(const std::string str)
{
        Json::Value root;
        Json::Reader reader;

       if (reader.parse(str, root ) == false)
                return false;
                
	if (root.isMember("maxpanes") == false && root["maxpanes"].isNumeric())
		return false;
	m_maxpanes = root["maxpanes"].asInt();

	return true;
}


