#include <Geode/Geode.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <miskaa.notif/src/includes/notif.hpp>
#include <alphalaneous.level-storage-api/include/LevelStorageAPI.hpp>

using namespace geode::prelude;

static constexpr const char* BUDGET_KEY = "budget";
static int getBudget(CCLayer* layer) {
    return alpha::level_storage::getSavedValue<int>(layer, BUDGET_KEY);
}
static void saveBudget(LevelEditorLayer* layer, int budget) {
    alpha::level_storage::setSavedValue(layer, BUDGET_KEY, budget);
}

// BudgetPopup
class BudgetPopup : public geode::Popup {
protected:
    LevelEditorLayer* m_editorLayer = nullptr;
    geode::TextInput* m_input = nullptr;
    bool init(LevelEditorLayer* editorLayer) {
        if (!Popup::init(280.f, 185.f)) return false;
        m_editorLayer = editorLayer;
        this->setTitle("Set Object Count Budget");
        auto cs = m_mainLayer->getContentSize();
        float cx = cs.width / 2.f, cy = cs.height / 2.f;
        m_input = geode::TextInput::create(180.f, "Enter limit...", "bigFont.fnt");
        m_input->setFilter("0123456789");
        m_input->setMaxCharCount(9);
        m_input->setPosition(ccp(cx, cy + 12.f));
        m_input->setScale(0.75f);
        int current = getBudget(editorLayer);
        if (current > 0) m_input->setString(std::to_string(current));
        m_mainLayer->addChild(m_input);
        auto btnMenu = CCMenu::create();
        btnMenu->setPosition(ccp(cx, cy - 42.f));
        m_mainLayer->addChild(btnMenu);
        auto addBtn = [&](const char* text, const char* bg, float x, SEL_MenuHandler sel) {
            auto spr = ButtonSprite::create(text, "bigFont.fnt", bg, 0.55f);
            auto item = CCMenuItemSpriteExtra::create(spr, this, sel);
            item->setPositionX(x);
            btnMenu->addChild(item);
        };
        addBtn("Set",    "GJ_button_01.png", -83.f, menu_selector(BudgetPopup::onSet));
        addBtn("Clear",  "GJ_button_06.png",   0.f, menu_selector(BudgetPopup::onClear));
        addBtn("Cancel", "GJ_button_05.png",  83.f, menu_selector(BudgetPopup::onCancel));
        return true;
    }
    void onSet(CCObject*) {
        auto res = geode::utils::numFromString<int>(m_input->getString());
        if (!res || *res <= 0) {FLAlertLayer::create("Error", res ? "Budget must be greater than 0" : "Please enter a number :sob:", "OK")->show(); return;}
        saveBudget(m_editorLayer, *res);
        this->onClose(nullptr);
        FLAlertLayer::create("Budget Set", fmt::format("Object budget is now set to <cy>{}</c>!", *res).c_str(), "OK")->show();
    }
    void onClear(CCObject*) {
        saveBudget(m_editorLayer, 0);
        this->onClose(nullptr);
        FLAlertLayer::create("Budget Cleared", "Object budget removed.", "OK")->show();
    }
    void onCancel(CCObject*) { this->onClose(nullptr); }
public:
    static BudgetPopup* create(LevelEditorLayer* editorLayer) {
        auto ret = new BudgetPopup();
        if (ret->init(editorLayer)) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }
};

// LevelEditorLayer
class $modify(MyLevelEditorLayer, LevelEditorLayer) {
    struct Fields {
        bool m_budgetIgnored = false;
        int m_lastNotifMilestone = 0;
        bool m_alertOpen = false;
    };
    GameObject* createObject(int key, CCPoint position, bool noUndo) {
        auto obj = LevelEditorLayer::createObject(key, position, noUndo);
        if (!obj || !m_level) return obj;
        int budget = getBudget(this);
        if (budget <= 0) return obj;
        int count = m_objectCount.value();
        int pct = (int)((float)count / (float)budget * 100.f);

        if (pct < 75) m_fields->m_lastNotifMilestone = 0;
        
        auto fireNotif = [](const std::string& text) {notifapi::notif::create(text, "warning", 3.5f, {0, 0, 0}, 1.0f, notifapi::Position::TopCenter, notifapi::Animation::Slide, "", 0.f)->show();};
        if (pct >= 95 && m_fields->m_lastNotifMilestone < 95) { m_fields->m_lastNotifMilestone = 95; fireNotif("95% of budget reached!"); }
        else if (pct >= 85 && m_fields->m_lastNotifMilestone < 85) { m_fields->m_lastNotifMilestone = 85; fireNotif("85% of budget reached!"); }
        else if (pct >= 75 && m_fields->m_lastNotifMilestone < 75) { m_fields->m_lastNotifMilestone = 75; fireNotif("75% of budget reached!"); }
        if (count >= budget && !m_fields->m_budgetIgnored && !m_fields->m_alertOpen) {
            m_fields->m_alertOpen = true;
            geode::createQuickPopup(
                fmt::format("{} budget reached", budget).c_str(),
                "You reached the limit you've set, continue?",
                "No", "Yes",
                [this](auto, bool yes) {
                    m_fields->m_alertOpen = false;
                    if (yes) m_fields->m_budgetIgnored = true;
                }
            );
        }
        return obj;
    }
};

// EditorPauseLayer
class $modify(MyEditorPauseLayer, EditorPauseLayer) {
    void onOpenBudget(CCObject*) {
        if (!m_editorLayer || !m_editorLayer->m_level) return;
        BudgetPopup::create(m_editorLayer)->show();
    }
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorPauseLayer::init(editorLayer)) return false;
        if (!m_editorLayer || !m_editorLayer->m_level) return true;
        int budget = getBudget(m_editorLayer);
        int count = m_editorLayer->m_objectCount.value();
        // find obj count label
        auto findLabel = [this]() -> CCNode* {
            if (auto* n = this->getChildByIDRecursive("object-count-label")) return n;
            if (auto* infoMenu = this->getChildByIDRecursive("info-menu")) {
                for (auto* child : CCArrayExt<CCNode*>(infoMenu->getChildren())) {
                    auto* lbl = dynamic_cast<CCLabelBMFont*>(child);
                    if (lbl && std::string(lbl->getString()).find("bjects") != std::string::npos)
                        return lbl;
                }
            }
            return nullptr;
        };
        // append budget info if budget set
        if (budget > 0) {
            int pct = (int)((float)count / (float)budget * 100.f);
            if (auto* lbl = typeinfo_cast<CCLabelBMFont*>(findLabel())) {
                std::string existing = lbl->getString();
                if (existing.find("(/") == std::string::npos)
                    lbl->setString((existing + fmt::format(" (/{} - {}%)", budget, pct)).c_str());
            }
        }
        // make label clickable
        if (auto* labelNode = findLabel()) {
            auto* parent = labelNode->getParent();
            auto labelSize = labelNode->getContentSize();
            auto hitSize = CCSize(labelSize.width + 20.f, labelSize.height + 16.f);
            auto hitSpr = CCSprite::create();
            hitSpr->setContentSize(hitSize);
            auto* btn = CCMenuItemSpriteExtra::create(hitSpr, this, menu_selector(MyEditorPauseLayer::onOpenBudget));
            btn->setPosition(labelNode->getPosition());
            btn->setScale(labelNode->getScale());
            btn->setContentSize(hitSize);
            auto* menu = CCMenu::create();
            menu->setPosition({0, 0});
            menu->setZOrder(labelNode->getZOrder() + 1);
            menu->addChild(btn);
            parent->addChild(menu);
        }
        return true;
    }
};