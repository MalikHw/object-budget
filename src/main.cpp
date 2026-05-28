#include <Geode/Geode.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <miskaa.notif/src/includes/notif.hpp>

using namespace geode::prelude;

// Per-level budget save helpers

static std::string budgetKey(int levelID) {
    return "budget_" + std::to_string(levelID);
}
static int getBudget(int levelID) {
    return Mod::get()->getSavedValue<int>(budgetKey(levelID), 0);
}
static void saveBudget(int levelID, int budget) {
    Mod::get()->setSavedValue<int>(budgetKey(levelID), budget);
}

// BudgetPopup
class BudgetPopup : public geode::Popup<int> {
protected:
    int m_levelID = 0;
    CCTextInputNode* m_input = nullptr;
    bool setup(int levelID) override {
        m_levelID = levelID;
        this->setTitle("Set Object Count Budget");
        auto cs = m_mainLayer->getContentSize();
        float cx = cs.width  / 2.f;
        float cy = cs.height / 2.f;
        // input
        m_input = CCTextInputNode::create(180.f, 40.f, "Enter limit...", "bigFont.fnt");
        m_input->setAllowedChars("0123456789");
        m_input->setMaxLabelLength(9);
        m_input->setPosition(ccp(cx, cy + 12.f));
        m_input->setScale(0.75f);
        int current = getBudget(levelID);
        if (current > 0)
            m_input->setString(std::to_string(current).c_str());
        m_mainLayer->addChild(m_input);
        // btn's
        auto btnMenu = CCMenu::create();
        btnMenu->setPosition(ccp(cx, cy - 42.f));
        m_mainLayer->addChild(btnMenu);
        auto addBtn = [&](const char* text, const char* bg, float x, SEL_MenuHandler sel) {
            auto spr  = ButtonSprite::create(text, "bigFont.fnt", bg, 0.7f);
            auto item = CCMenuItemSpriteExtra::create(spr, this, sel);
            item->setPositionX(x);
            btnMenu->addChild(item);
        };
        addBtn("Set", "GJ_button_01.png", -62.f, menu_selector(BudgetPopup::onSet));
        addBtn("Clear", "GJ_button_06.png", 0.f, menu_selector(BudgetPopup::onClear));
        addBtn("Cancel", "GJ_button_05.png", 62.f, menu_selector(BudgetPopup::onCancel));

        return true;
    }
    void onSet(CCObject*) {
        std::string str = m_input->getString();
        if (str.empty()) {
            FLAlertLayer::create("Error", "Please enter a number :sob:", "OK")->show();
            return;
        }
        int budget = std::stoi(str);
        if (budget <= 0) {
            FLAlertLayer::create("Error", "Budget must be greater than 0", "OK")->show();
            return;
        }
        saveBudget(m_levelID, budget);
        this->onClose(nullptr);
        FLAlertLayer::create(
            "Budget Set",
            fmt::format("Object budget is now set to <cy>{}</c>!", budget).c_str(),
            "OK"
        )->show();
    }
    void onClear(CCObject*) {
        saveBudget(m_levelID, 0);
        this->onClose(nullptr);
        FLAlertLayer::create("Budget Cleared", "Object budget removed.", "OK")->show();
    }
    void onCancel(CCObject*) {
        this->onClose(nullptr);
    }

public:
    static BudgetPopup* create(int levelID) {
        auto ret = new BudgetPopup();
        if (ret->initAnchored(280.f, 185.f, levelID)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

// Budget alert delegate
class BudgetAlertDelegate : public CCObject, public FLAlertLayerProtocol {
public:
    // btn2 is true then "Yes" (continue / ignore budget this session)
    // btn2 is false then "No" (alert will fire again next placement)
    std::function<void(bool)> callback;
    static BudgetAlertDelegate* create(std::function<void(bool)> cb) {
        auto* d = new BudgetAlertDelegate();
        d->callback = std::move(cb);
        d->autorelease();
        return d;
    }
    void FLAlert_Clicked(FLAlertLayer*, bool btn2) override {
        if (callback) callback(btn2);
    }
};

// budget button
class $modify(MyEditLevelLayer, EditLevelLayer) {
    void onBudget(CCObject*) {
        if (!m_level) return;
        BudgetPopup::create(m_level->m_levelID)->show();
    }
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;
        if (!m_buttonMenu) return true;
        // fallback since writing AND lazy to add a png until later
        CCMenuItemSpriteExtra* btn = nullptr;
        auto customSpr = CCSprite::createWithSpriteFrameName("budget-btn.png");
        if (customSpr) {
            btn = CCMenuItemSpriteExtra::create(
                customSpr, this,
                menu_selector(MyEditLevelLayer::onBudget)
            );
        } else {
            auto spr = ButtonSprite::create("Budget", "bigFont.fnt", "GJ_button_04.png", 0.55f);
            btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(MyEditLevelLayer::onBudget));
        }
        m_buttonMenu->addChild(btn);
        m_buttonMenu->updateLayout();
        return true;
    }
};

// LevelEditorLayer
class $modify(MyLevelEditorLayer, LevelEditorLayer) {
    struct Fields {
        // Set true when user clicks "Yes" (ignore for the session)
        bool m_budgetIgnored       = false;
        // Tracks the highest % milestone already notified (75/85/95)
        int  m_lastNotifMilestone  = 0;
        // Prevent re-entrant alert while one is already open
        bool m_alertOpen           = false;
    };
    GameObject* createObject(int key, CCPoint position, bool noUndo) {
        auto obj = LevelEditorLayer::createObject(key, position, noUndo);
        if (!obj || !m_level) return obj;
        int budget = getBudget(m_level->m_levelID);
        if (budget <= 0) return obj;
        int count = m_objectCount.value();
        auto& f = *m_fields;
        // NOTIFICATIONS! (miskaa.api again)
        int pct = (int)((float)count / (float)budget * 100.f);
        // reset milestone if the count drops back
        if (pct < 75) f.m_lastNotifMilestone = 0;
        else if (pct < 85) f.m_lastNotifMilestone = std::min(f.m_lastNotifMilestone, 74);
        else if (pct < 95) f.m_lastNotifMilestone = std::min(f.m_lastNotifMilestone, 84);
        auto fireNotif = [](const std::string& text) {
            notifapi::notif::create(text, "warning", 3.5f, {0, 0, 0}, 1.0f, notifapi::Position::TopCenter, notifapi::Animation::Slide, "", 0.f)->show();
        };
        if (pct >= 95 && f.m_lastNotifMilestone < 95) { f.m_lastNotifMilestone = 95; fireNotif("95% of budget reached!"); } else if (pct >= 85 && f.m_lastNotifMilestone < 85) { f.m_lastNotifMilestone = 85; fireNotif("85% of budget reached!"); } else if (pct >= 75 && f.m_lastNotifMilestone < 75) { f.m_lastNotifMilestone = 75; fireNotif("75% of budget reached!"); }
        // budget reached reached alert
        if (count >= budget && !f.m_budgetIgnored && !f.m_alertOpen) {
            f.m_alertOpen = true;
            this->retain();
            auto* delegate = BudgetAlertDelegate::create([this](bool yes) {
                m_fields->m_alertOpen = false;
                if (yes) {
                    // yes means ignore budget for the rest of this session
                    m_fields->m_budgetIgnored = true;
                }
                this->release();
            });
            // retain delegate so it stays alive while alert is shown
            delegate->retain();
            auto alert = FLAlertLayer::create(delegate, fmt::format("{} budget reached", budget).c_str(), "You reached the limit you've set, continue?", "Yes", "No");
            alert->m_noElasticity = true;
            alert->show();
        }
        return obj;
    }
};

// EditorPauseLayer
class $modify(MyEditorPauseLayer, EditorPauseLayer) {
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorPauseLayer::init(editorLayer)) return false;
        if (!m_editorLayer || !m_editorLayer->m_level) return true;
        int budget = getBudget(m_editorLayer->m_level->m_levelID);
        if (budget <= 0) return true;
        int count = m_editorLayer->m_objectCount.value();
        int pct   = (int)((float)count / (float)budget * 100.f);
        std::string append = fmt::format(" (/{} - {}%)", budget, pct);
        // geode.node-ids sees object-count-label
        CCNode* labelNode = this->getChildByIDRecursive("object-count-label");
        // fallback
        if (!labelNode) {
            CCNode* infoMenu = this->getChildByIDRecursive("info-menu");
            if (infoMenu && infoMenu->getChildren()) {
                CCObject* child = nullptr;
                CCARRAY_FOREACH(infoMenu->getChildren(), child) {
                    auto* lbl = dynamic_cast<CCLabelBMFont*>(child);
                    if (!lbl) continue;
                    std::string s = lbl->getString();
                    if (s.find("bjects") != std::string::npos) { // "Objects" or "objects" since idk lol
                        labelNode = lbl;
                        break;
                    }
                }
            }
        }
        if (auto* lbl = typeinfo_cast<CCLabelBMFont*>(labelNode)) {
            std::string existing = lbl->getString();
            if (existing.find("(/") == std::string::npos) {
                lbl->setString((existing + append).c_str());
            }
        }
        return true;
    }
};