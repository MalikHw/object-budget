#include <Geode/Geode.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <miskaa.notif/src/includes/notif.hpp>
#include <alphalaneous.level-storage-api/include/LevelStorageAPI.hpp>

using namespace geode::prelude;

static constexpr const char* BUDGET_KEY = "malikhw47.object-budget/budget";

static int getBudget(CCLayer* layer) {return alpha::level_storage::getSavedValue<int>(layer, BUDGET_KEY);}
static void saveBudget(LevelEditorLayer* layer, int budget) {alpha::level_storage::setSavedValue(layer, BUDGET_KEY, budget);}

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
        addBtn("Set", "GJ_button_01.png", -83.f, menu_selector(BudgetPopup::onSet));
        addBtn("Clear", "GJ_button_06.png", 0.f, menu_selector(BudgetPopup::onClear));
        addBtn("Cancel", "GJ_button_05.png", 83.f, menu_selector(BudgetPopup::onCancel));
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
        bool m_budgetCheckPending = false;
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

        // Defer the over-budget popup to end of frame so bulk placements
        // (e.g. custom objects) only ever produce one popup, not a spam.
        if (count >= budget && !m_fields->m_budgetIgnored && !m_fields->m_alertOpen && !m_fields->m_budgetCheckPending) {
            m_fields->m_budgetCheckPending = true;
            Loader::get()->queueInMainThread([this, budget] {
                m_fields->m_budgetCheckPending = false;
                if (m_fields->m_budgetIgnored || m_fields->m_alertOpen) return;
                int currentCount = m_objectCount.value();
                if (currentCount < budget) return;
                m_fields->m_alertOpen = true;
                geode::createQuickPopup(
                    fmt::format("{} budget reached", budget).c_str(), "You reached the limit you've set, continue?", "No", "Yes",
                    [this](auto, bool yes) {m_fields->m_alertOpen = false; if (yes) m_fields->m_budgetIgnored = true;}
                );
            });
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
        auto* lbl = typeinfo_cast<CCLabelBMFont*>(this->getChildByIDRecursive("object-count-label"));
        if (!lbl) return true;
        // append budget info if a limit is set
        int budget = getBudget(m_editorLayer);
        if (budget > 0) {
            int count = m_editorLayer->m_objectCount.value();
            int pct = (int)((float)count / (float)budget * 100.f);
            lbl->setString((std::string(lbl->getString()) + fmt::format(" (/{} - {}%)", budget, pct)).c_str());
        }
        // make the label THE button
        auto* parent = lbl->getParent();
        auto pos = lbl->getPosition();
        auto z = lbl->getZOrder();
        lbl->retain();
        parent->removeChild(lbl, false);
        auto* btn = CCMenuItemSpriteExtra::create(lbl, nullptr, this, menu_selector(MyEditorPauseLayer::onOpenBudget));
        lbl->release();
        btn->setPosition(ccp(300.f, 90.f));
        btn->setZOrder(z);
        parent->addChild(btn);
        return true;
    }
};