%module CameraClientWrapper
%{
using namespace std;
#include <stdint.h>
#include <json/json.h>
#include <libclientserver.h>
#include <WebStreamType.h>
#include <WebStreamOptions.h>
#include <VideoInputSupported.h>
#include <VideoInputConfig.h>
#include <UserItem.h>

#include "CameraClient.h"
%}

%include <std_vector.i>
%include <std_map.i>
%include <std_pair.i>
%include <std_string.i>
%include <typemaps.i>
%include <stl.i>

%apply int *OUTPUT { int *};
%apply long { time_t };

%apply int { time_t };

%template(StringVector) std::vector<std::string>;

%include CameraClient.h
%include VideoInputSupported.h
%include VideoInputConfig.h
%include UserItem.h
%include WebStreamType.h
%include WebStreamOptions.h

