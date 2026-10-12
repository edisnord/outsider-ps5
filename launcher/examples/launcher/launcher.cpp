/*
 * Outsider PS5 - the game list.
 *
 * Lists the RPG Maker MV and MZ games on the console and on USB sticks, and starts the chosen one in Outsider
 * (outsider.bin, the title's other program): the choice is written to /data/outsider-ps5/launch.txt, which
 * Outsider's start-up (runtime/vk_main.c) reads, and the shell starts that program in place of this one. Drawn
 * with the base class's overlay on the PS5 Vulkan Template's foundation: the D-pad moves, CROSS chooses.
 *
 * Copyright (C) 2026 Mihawk (the foundation), 2026 the Outsider PS5 contributors
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "vulkanexamplebase.h"

#include <algorithm>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern "C" int sceSystemServiceLoadExec(const char *path, const char *const *argv);

namespace {

const char *const dataDir = "/data/outsider-ps5";
const char *const program = "/app0/outsider.bin";
const char *const shims = "/app0/outsider/shims";
const char *const roots[] = {
	"/data/outsider-ps5", "/mnt/usb0/games", "/mnt/usb1/games", "/mnt/usb2/games", "/mnt/usb3/games",
	"/mnt/usb4/games", "/mnt/usb5/games", "/mnt/usb6/games", "/mnt/usb7/games",
};

struct Game {
	std::string name;
	std::string root; // the folder with index.html: what Outsider is given
};

bool exists(const std::string &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}

// Folders under a root that hold an MV or MZ game: index.html in the folder, or in its www/
std::vector<Game> findGames()
{
	std::vector<Game> games;
	for (const char *root : roots) {
		DIR *dir = opendir(root);
		if (!dir) {
			continue;
		}
		while (dirent *entry = readdir(dir)) {
			if (entry->d_name[0] == '.') {
				continue;
			}
			const std::string folder = std::string(root) + "/" + entry->d_name;
			if (exists(folder + "/www/index.html")) {
				games.push_back({ entry->d_name, folder + "/www" });
			} else if (exists(folder + "/index.html")) {
				games.push_back({ entry->d_name, folder });
			}
		}
		closedir(dir);
	}
	std::sort(games.begin(), games.end(), [](const Game &a, const Game &b) { return a.name < b.name; });
	return games;
}

// The hand-off's words are split at spaces that are not escaped with a backslash
std::string escaped(const std::string &s)
{
	std::string out;
	for (char c : s) {
		if (c == ' ' || c == '\\') {
			out += '\\';
		}
		out += c;
	}
	return out;
}

} // namespace

class VulkanExample : public VulkanExampleBase
{
public:
	std::vector<Game> games;
	std::string message;

	VulkanExample() : VulkanExampleBase()
	{
		title = "Outsider PS5";
		apiVersion = VK_API_VERSION_1_3;
		useDynamicRendering = true;
		requiresStencil = true;
		deviceCreatepNextChain = &baseDynamicRenderingFeatures;
	}

	void prepare() override
	{
		VulkanExampleBase::prepare();
		games = findGames();
		prepared = true;
	}

	// Writes the hand-off and asks the shell to start Outsider. Returns only when that failed.
	void start(const Game &game)
	{
		mkdir(dataDir, 0777);
		const std::string home = std::string(dataDir) + "/outsider";
		mkdir(home.c_str(), 0777);
		const std::string launch = std::string(dataDir) + "/launch.txt";
		FILE *f = fopen(launch.c_str(), "w");
		if (!f) {
			message = "Cannot write " + launch;
			return;
		}
		std::string args = "--game " + escaped(game.root) + " --shims " + shims;
		/* Diagnostics are off unless the file diag exists, so a recording sees none of their cost */
		if (exists(std::string(dataDir) + "/diag"))
			args += " --perf 120";
		if (exists(home + "/profile")) {
			args += " --profile 5";
		}
		fprintf(f, "%s\n%s\n%s\n", home.c_str(), ("HOME=" + escaped(home) + " PS5PATH_CASE_INSENSITIVE=1").c_str(),
		        args.c_str());
		fclose(f);
		fflush(nullptr);
		static const char *const noArgs[] = { nullptr };
		const int result = sceSystemServiceLoadExec(program, noArgs);
		if (result >= 0) {
			for (;;) {
				usleep(100000); // the shell replaces this program
			}
		}
		unlink(launch.c_str());
		message = "The console refused to start " + std::string(program);
	}

	void buildCommandBuffer()
	{
		VkCommandBuffer cmdBuffer = drawCmdBuffers[currentBuffer];
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));
		beginDynamicRendering(cmdBuffer);
		VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
		vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
		VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
		vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);
		drawUI(cmdBuffer);
		endDynamicRendering(cmdBuffer);
		VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));
	}

	void render() override
	{
		if (!prepared) {
			return;
		}
		VulkanExampleBase::prepareFrame();
		buildCommandBuffer();
		VulkanExampleBase::submitFrame();
	}

	void OnUpdateUIOverlay(vks::UIOverlay *overlay) override
	{
		if (overlay->header("Games")) {
			if (games.empty()) {
				overlay->text("No games found.");
				overlay->text("Put MV or MZ game folders in /data/outsider-ps5 or in a games folder on a USB stick.");
			}
			for (const Game &game : games) {
				if (overlay->button(game.name.c_str())) {
					start(game);
				}
			}
		}
		if (!message.empty()) {
			overlay->text("%s", message.c_str());
		}
		if (overlay->button("Rescan")) {
			games = findGames();
		}
		if (overlay->button("Quit")) {
			quit = true;
		}
	}
};

VULKAN_EXAMPLE_MAIN()
