// Copyright (c) 2014-2021 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include <sstream>
#include <locale>

#include <glib.h>
#include <time.h>

#include "utils.h"
#include "logging.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <cstdio>


using namespace std;

std::vector<std::string> split(const std::string &s, char delim)
{
	std::vector<std::string> elems;
	std::stringstream ss(s);
	std::string item;
	while (std::getline(ss, item, delim))
	{
		elems.push_back(item);
	}
	return elems;
}

std::string convertToLower(const std::string &input)
{
	std::string output;
	std::locale loc;
	for (std::string::size_type i=0; i<input.length(); ++i)
		output += std::tolower(input[i],loc);
	return output;
}

std::string convertToUpper(const std::string &input)
{
	std::string output;
	std::locale loc;
	for (std::string::size_type i=0; i<input.length(); ++i)
		output += std::toupper(input[i],loc);
	return output;
}

bool checkPathExists(const std::string &path)
{
	if (path.length() == 0)
		return false;

	gchar *fileBasePath = g_path_get_dirname(path.c_str());
	if (!fileBasePath)
		return false;

	bool result = g_file_test(fileBasePath, (GFileTest)(G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR));

	g_free(fileBasePath);

	return result;
}

bool checkFileIsValid(const std::string &path)
{
	std::string testPath = path;

	if (testPath.length() == 0)
		return false;

	if (g_file_test(path.c_str(), (GFileTest) G_FILE_TEST_IS_SYMLINK))
		return false;

	return  g_file_test(testPath.c_str(), (GFileTest)(G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR));
}

/**
*      write_klog
*
*      @name   write_kernel log
*      @param  message [in] string for kernel
*      @return NULL
*/
void write_kernel_log(const char *message)
{
   int fd;
   char buffer[1024];


	fd = open("/dev/kmsg", O_RDWR);
	if( fd<0 ) {
		BT_DEBUG("open fail : /dev/kmsg\n");
		return;
	}

	std::strncat(buffer, message, sizeof(buffer)-1);
	auto ret = write(fd, buffer, strlen(buffer)+1);
	static_cast<void>(ret);
	close(fd);
}

/**
*      print bluetooth ready log to kernel
*
*      @name   bt_ready_msg2kernel
*      @param  message [in] string for kernel
*      @return NULL
*/
void bt_ready_msg2kernel(void)
{
#define MAX_STRING_SIZE 128
	char logBuf[MAX_STRING_SIZE] = "";
	time_t sec;
	long msec;
	struct timespec ct;

	clock_gettime(CLOCK_MONOTONIC, &ct);
	sec = ct.tv_sec;
	msec = ct.tv_nsec/1000000.0;
	snprintf(logBuf, sizeof(logBuf), "Get BTUSB_READY %ld.%ld PerfType:WBS PerfGroup:bt_ready \n", (long)sec, msec );
	BT_DEBUG("Get BTUSB_READY %ld.%ld PerfType:BtMngr PerfGroup:BT_INITIALIZED \n", (long)sec, msec );
	write_kernel_log(logBuf);
}

std::string replaceString(std::string subject, const std::string& search, const std::string& replace) {
    size_t pos = 0;
    while ((pos = subject.find(search, pos)) != std::string::npos) {
         subject.replace(pos, search.length(), replace);
         pos += replace.length();
    }
    return subject;
}

bool changeGroup(const std::string &groupName, const std::string &fileName)
{
	std::string testfileName = fileName;

	if (testfileName.length() == 0)
		return false;

	if (g_file_test(fileName.c_str(), (GFileTest) G_FILE_TEST_IS_SYMLINK))
		return false;

	if (!g_file_test(testfileName.c_str(), (GFileTest)(G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR)))
		return false;

	std::string command = "chgrp " + groupName + " " + testfileName;
	auto ret = system(command.c_str());
	static_cast<void>(ret);
	return true;

}

bool changeFilePermission(const std::string &permission, const std::string &fileName)
{
	std::string testfileName = fileName;

	if (testfileName.length() == 0)
		return false;

	if (g_file_test(fileName.c_str(), (GFileTest) G_FILE_TEST_IS_SYMLINK))
		return false;

	if (!g_file_test(testfileName.c_str(), (GFileTest)(G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR)))
		return false;

	std::string command = "chmod " + permission + " " + testfileName;
	auto ret = system(command.c_str());
	static_cast<void>(ret);
	return true;
}

bool changeFolderPermission(const std::string &permission, const std::string &folderName)
{
	std::string testfolderName = folderName;

	if (testfolderName.length() == 0)
		return false;

	if (g_file_test(folderName.c_str(), (GFileTest) G_FILE_TEST_IS_SYMLINK))
		return false;

	if (!g_file_test(testfolderName.c_str(), (GFileTest)(G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR)))
		return false;

	std::string command = "chmod -R " + permission + " " + testfolderName;
	auto ret = system(command.c_str());
	static_cast<void>(ret);
	return true;
}

bool changeFolderGroup(const std::string &groupName, const std::string &folderName)
{
	std::string testfolderName = folderName;

	if (testfolderName.length() == 0)
		return false;

	if (g_file_test(folderName.c_str(), (GFileTest) G_FILE_TEST_IS_SYMLINK))
		return false;

	if (!g_file_test(testfolderName.c_str(), (GFileTest)(G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR)))
		return false;

	std::string command = "chgrp -R " + groupName + " " + testfolderName;
	auto ret = system(command.c_str());
        static_cast<void>(ret);
	return true;

}
