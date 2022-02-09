#include "pch.h"
#include <EventAPI.h>
#include <LoggerAPI.h>
#include <MC/Level.hpp>
#include <MC/BlockInstance.hpp>
#include <MC/Block.hpp>
#include <MC/BlockSource.hpp>
#include <MC/Actor.hpp>
#include <MC/Player.hpp>
#include <MC/ItemStack.hpp>
#include <LLAPI.h>
#include <MC/AbilityCommand.hpp>
#include <MC/Level.hpp>
#include <unordered_map>
#include <MC/ServerPlayer.hpp>
#include <RegCommandAPI.h>
#include <PlayerInfoAPI.h>
#include "../SDK/Header/third-party/Nlohmann/json.hpp"
using namespace RegisterCommandHelper;

static std::unordered_map<Player*, bool> Flying;
using nlohmann::json;

Logger logger("Survival_Fly");
bool onlyOp = false, onlyAllow = false, slowFalling = true;
int Falling = 3;
json allowList = {};
bool readFile() {
	std::ifstream jsonFile("plugins/SurvivalFly/config.json");
	if (!jsonFile.is_open()) {
		return false;
	}
	json config;
	jsonFile >> config;

	onlyOp = config["onlyOp"];
	onlyAllow = config["onlyAllow"];
	slowFalling = config["slowFalling"];
	allowList = config["allowList"];
	Falling = config["FallingPoint"].get<int>();
	if (Falling > 0) {
		Falling *= -1;
	}
	return true;
}

bool getArrayL(string xuid) {
	for (auto it = allowList.begin(); it != allowList.end(); ++it) {
		string selectName = it.value().get<string>();
		if (selectName == xuid) {
			return true;
		}
	}
	return false;
}

//注册指令
class MayflyCommand : public Command {
	enum MayflyOP :int {
		add = 1,
		set = 2,
		cancel = 3,
	} op;
	string dst;
	int state;
public:
	void execute(CommandOrigin const& ori, CommandOutput& output) const override {//执行部分
		string dsxuid;
		switch (op) {
			case add:
				dsxuid = PlayerInfo::getXuid(dst);
				if (dsxuid == "") {
					output.addMessage("Not found target");
					return;
				}
				else {
					std::ifstream jsonFile("plugins/SurvivalFly/config.json");
					json config;
					jsonFile >> config;
					config["allowList"].push_back(dsxuid);
					std::ofstream ojsonFile("plugins/SurvivalFly/config.json");
					ojsonFile << std::setw(4) << config;

					output.addMessage("Added \""+dst+"\" to allowList");
					return;
				}
				break;
			case set:
				for (auto p : Level::getAllPlayers()) {
					if (p->getName() == dst) {
						if (!state) {
							Flying[p] = false;
							Level::runcmdEx("ability \"" + p->getName() + "\" mayfly false");
						}
						else {
							Flying[p] = true;
							Level::runcmdEx("ability \"" + p->getName() + "\" mayfly true");
						}
						
						output.addMessage("Seted \"" + dst + "\" mayfly "+std::to_string((bool)state));
						return;
					}
				}
				output.addMessage("Not found target");
				return;
				break;
			case cancel:
				dsxuid = PlayerInfo::getXuid(dst);
				if (dsxuid == "") {
					output.addMessage("Not found target");
					return;
				}
				else {
					std::ifstream jsonFile("plugins/SurvivalFly/config.json");
					json config;
					jsonFile >> config;
					config["allowList"].erase(dsxuid);
					std::ofstream ojsonFile("plugins/SurvivalFly/config.json");
					ojsonFile << std::setw(4) << config;

					output.addMessage("canceled \"" + dst + "\" to allowList");
					return;
				}
				break;
		}
	}

	static void setup(CommandRegistry* registry) {//注册部分(推荐做法)
		registry->registerCommand("mayfly", "Survival fly setup", CommandPermissionLevel::GameMasters, { (CommandFlagValue)0 }, { (CommandFlagValue)0x80 });
		registry->addEnum<MayflyOP>("Mayfly1", { { "add",MayflyOP::add},{ "cancel",MayflyOP::cancel} });
		registry->addEnum<MayflyOP>("Mayfly2", { { "set", MayflyOP::set} });
		
		registry->registerOverload<MayflyCommand>(
			"mayfly",
			makeMandatory<CommandParameterDataType::ENUM>(&MayflyCommand::op, "optional", "Mayfly1"),
			makeMandatory(&MayflyCommand::dst, "PlayerName")
		);
		registry->registerOverload<MayflyCommand>(
			"mayfly",
			makeMandatory<CommandParameterDataType::ENUM>(&MayflyCommand::op, "optional", "Mayfly2"),
			makeMandatory(&MayflyCommand::dst, "PlayerName"),
			makeMandatory(&MayflyCommand::state, "State"));
	}
};

void PluginInit()
{
	LL::registerPlugin("Survival_Fly", "Fly in Suvival mode.", LL::Version(0, 0, 1));
	bool rf = readFile();
	if(!rf){
		logger.error("Failed to read config file,please check.");
	}
	logger.info("Loaded.");
	Event::PlayerJoinEvent::subscribe([](Event::PlayerJoinEvent ev) {
		Player* pl = ev.mPlayer;
		if (onlyOp && pl->isOP()) {
			Flying[pl] = true;
			Level::runcmdEx("ability \"" + pl->getName() + "\" mayfly true");
		}
		else if (onlyAllow && getArrayL(pl->getXuid())) {
			Flying[pl] = true;
			Level::runcmdEx("ability \"" + pl->getName() + "\" mayfly true");
		}
		else if (onlyOp == false && onlyAllow == false) {
			Flying[pl] = true;
			Level::runcmdEx("ability \"" + pl->getName() + "\" mayfly true");
		}
		else {
			Flying[pl] = false;
		}
		return true;
	});

	Event::RegCmdEvent::subscribe([](Event::RegCmdEvent ev) {
		MayflyCommand::setup(ev.mCommandRegistry);
		return true;
	});
}

//强开ability
THook(void, "?setup@ChangeSettingCommand@@SAXAEAVCommandRegistry@@@Z",
	void* self) {
	SymCall("?setup@AbilityCommand@@SAXAEAVCommandRegistry@@@Z", void, void*)(self);
	return original(self);
}

//Tick
int tickNum = 0;
THook(void, "?tick@Level@@UEAAXXZ", void* self) {
	original(self);
	if (tickNum >= 20) {
		if (slowFalling) {
			for (auto p : Level::getAllPlayers()) {
				if (Flying[p]) {
					Block* bl = Level::getBlock(p->getBlockPos().add(0, Falling, 0), p->getDimensionId());
					if (bl->getTypeName() == "minecraft:air") {
						Level::runcmdEx("effect \"" + p->getName() + "\" slow_falling 1 1 true");
					}
					else {
						p->removeEffect(27);
					}
				}
			}
		}
	}
	else {
		tickNum++;
	}
	
}