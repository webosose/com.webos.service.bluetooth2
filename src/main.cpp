// Copyright (c) 2014-2024 LG Electronics, Inc.
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

#include "bluetoothpairstate.h"
#include "bluetoothmanagerservice.h"
#include "config.h"
#include "logging.h"
#include "utils.h"



PmLogContext logContext;
static const char* const logContextName = "webos-bluetooth-service";

static gboolean option_version = FALSE;

static GOptionEntry options[] = {
	{ "version", 'v', 0, G_OPTION_ARG_NONE, &option_version,
	  "Show version information and exit" },
	{ NULL },
};

static GMainLoop *mainLoop = nullptr;

extern "C" void term_handler(int signal)
{
	const char *str = nullptr;
	switch (signal) {
		case SIGTERM:
			str = "SIGTERM";
			break;
		case SIGABRT:
			str = "SIGABRT";
			break;
		case SIGINT:
			str = "SIGINT";
			break;
		default:
			str = "Unknown";
			break;
	}

	BT_DEBUG("signal received.. signal[%s]", str);
	g_main_loop_quit(mainLoop);
}

int main(int argc, char **argv)
{
	try
	{
		GOptionContext *context;
		GError *err = NULL;

		write_kernel_log("[bt_time] execute main ");

		context = g_option_context_new(NULL);
		g_option_context_add_main_entries(context, options, NULL);

		if (g_option_context_parse(context, &argc, &argv, &err) == FALSE) {
			if (err != NULL) {
				g_printerr("%s\n", err->message);
				g_error_free(err);
			} else
				g_printerr("An unknown error occurred\n");
			exit(1);
		}

		g_option_context_free(context);

		if (option_version == TRUE) {
			printf("%s\n", VERSION);
			exit(0);
		}

		PmLogErr error = PmLogGetContext(logContextName, &logContext);
		if (error != kPmLogErr_None)
		{
			fprintf(stderr, "Failed to setup up log context %s\n", logContextName);
			abort();
		}

		signal(SIGTERM, term_handler);
		signal(SIGINT, term_handler);
		mainLoop = g_main_loop_new(NULL, FALSE);

		BT_DEBUG("Starting bluetooth manager service");

		BluetoothManagerService manager; // manager creator throw the LS:Error but can't {}
        manager.attachToLoop(mainLoop);

		g_main_loop_run(mainLoop);

		g_main_loop_unref(mainLoop);

	}
	catch (const std::length_error& le) // CID 163501 (#1 of 5): Uncaught exception (UNCAUGHT_EXCEPT)
	{
		BT_ERROR(MSGID_LS2_FAILED_TO_SEND, 0, "Failed to Length error: %s", le.what());
	}
	catch (const LS::Error& error) // 	CID 163476 (#1 of 1): Uncaught exception (UNCAUGHT_EXCEPT)
	{
		BT_ERROR(MSGID_LS2_FAILED_TO_SEND, 0, "Failed to LS::Error: %s", error.what());
	}

	return 0;
}

// vim: noai:ts=4:sw=4:ss=4:expandtab
