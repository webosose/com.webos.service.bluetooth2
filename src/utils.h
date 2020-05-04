// Copyright (c) 2014-2018 LG Electronics, Inc.
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

#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>

std::vector<std::string> split(const std::string &s, char delim);
std::string convertToLower(const std::string &input);
std::string convertToUpper(const std::string &input);

bool checkPathExists(const std::string &path);
bool checkFileIsValid(const std::string &path);
std::string replaceString(std::string subject, const std::string& search, const std::string& replace);

void write_kernel_log(const char *message);
void bt_ready_msg2kernel(void);
#endif // UTILS_H
