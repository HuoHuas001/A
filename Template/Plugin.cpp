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
#include <MC/LevelData.hpp>
#include <MC/LevelSettings.hpp>
#include <MC/MinecraftEventing.hpp>
#include <MC/ActorDamageSource.hpp>
#include "../SDK/Header/third-party/Nlohmann/json.hpp"
#include <windows.h>
using namespace RegisterCommandHelper;

#define HOOK(name, ret, sym, ...)	

static std::unordered_map<string, bool> Flying;
using nlohmann::json;

Logger logger("Survival_Fly");
bool onlyOp = false, onlyAllow = false, slowFalling = true;
string particle = "";
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
	particle = config["particle"].get<string>();
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
		Player* p = Level::getPlayer(dst);
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
				if (p) {
					if (!state) {
						Flying[p->getXuid()] = false;
						Level::runcmdEx("ability \"" + p->getName() + "\" mayfly false");
					}
					else {
						Flying[p->getXuid()] = true;
						Level::runcmdEx("ability \"" + p->getName() + "\" mayfly true");
					}
						
					output.addMessage("Seted \"" + dst + "\" mayfly "+std::to_string((bool)state));
					return;
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

bool getPlayerInAir(Player* pl) {
	return (!pl->isOnGround()) && (!pl->isInWater());
}

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
			Flying[pl->getXuid()] = true;
			Level::runcmdEx("ability \"" + pl->getName() + "\" mayfly true");
		}
		else if (onlyAllow && getArrayL(pl->getXuid())) {
			Flying[pl->getXuid()] = true;
			Level::runcmdEx("ability \"" + pl->getName() + "\" mayfly true");
		}
		else if (onlyOp == false && onlyAllow == false) {
			Flying[pl->getXuid()] = true;
			Level::runcmdEx("ability \"" + pl->getName() + "\" mayfly true");
		}
		else {
			Flying[pl->getXuid()] = false;
		}
		return true;
	});

	Event::RegCmdEvent::subscribe([](Event::RegCmdEvent ev) {
		MayflyCommand::setup(ev.mCommandRegistry);
		return true;
	});

	Event::PlayerLeftEvent::subscribe([](Event::PlayerLeftEvent ev) {
		Flying[ev.mXUID] = false;
		return true;
	});

	if (slowFalling) {
		Event::MobHurtEvent::subscribe([](Event::MobHurtEvent ev) {
			Actor* ac = ev.mMob;
			if (ac->isPlayer()) {
				Player* pl = (Player*)ac;
				logger.info(pl->getPos().toString());
				if ((ev.mDamageSource->getCause() == ActorDamageCause::Fall) && Flying[pl->getXuid()]) {
					Level::runcmdEx("effect \"" + pl->getName() + "\" slow_falling 1 1 true");
				}
			}
			return true;
		});
	}

	if (particle != "") {
		Event::PlayerMoveEvent::subscribe([](Event::PlayerMoveEvent ev) {
			for (auto iter = Flying.begin(); iter != Flying.end(); iter++) {
				string xuid = iter->first;
				bool state = iter->second;
				if (state) {
					Player* pl = Level::getPlayer(xuid);
					if (pl) {
						Level::spawnParticleEffect(particle, pl->getPos(), Level::getDimension(pl->getDimensionId()));
					}
				}
			}
			return true;
		});
	}
}

//强开ability
THook(void, "?setup@ChangeSettingCommand@@SAXAEAVCommandRegistry@@@Z",
	void* self) {
	SymCall("?setup@AbilityCommand@@SAXAEAVCommandRegistry@@@Z", void, void*)(self);
	return original(self);
}

//强开教育版
THook(LevelSettings*, "?setEducationFeaturesEnabled@LevelSettings@@QEAAAEAV1@_N@Z",
	LevelSettings* _this, char a2) {
	*((BYTE*)_this + 84) = true;
	logger.info("Force to set education Features Enabled");
	return _this;
}


