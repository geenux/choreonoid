/**
   @author Shin'ichiro Nakaoka
*/

#include "../Item.h"
#include "../RootItem.h"
#include "../ExtCommandItem.h"
#include "../ItemList.h"
#include "../MessageView.h"
#include "../TaskView.h"
#include <cnoid/LuaUtil>
#include "exportdecl.h"

using namespace std;
using namespace cnoid;

namespace cnoid {

void exportLuaQtCoreTypes(sol::table& module);
void exportLuaQtGuiTypes(sol::table& module);
void exportLuaQtExTypes(sol::table& module);
void exportLuaMainWindowTypes(sol::table& module);
void exportLuaItems(sol::table& module);
void exportLuaViews(sol::table& module);
void exportLuaToolBarTypes(sol::table& module);
void exportLuaTimeBar(sol::table& module);

}

extern "C" CNOID_EXPORT int luaopen_cnoid_Base(lua_State* L)
{
    sol::state_view lua(L);

    lua["require"]("cnoid.Util");

    sol::table module = lua.create_table();

    exportLuaQtCoreTypes(module);
    exportLuaQtGuiTypes(module);
    exportLuaQtExTypes(module);
    exportLuaMainWindowTypes(module);
    exportLuaToolBarTypes(module);
    exportLuaTimeBar(module);

    module.new_usertype<View>(
        "View",
        sol::base_classes, sol::bases<QWidget, QObject>(),
        "new", sol::no_constructor,
        "name", &View::name,
        "isActive", &View::isActive,
        "bringToFront", &View::bringToFront,
        "lastFocusView", &View::lastFocusView
        );

    module.new_usertype<MessageView>(
        "MessageView",
        sol::base_classes, sol::bases<View, QWidget, QObject>(),
        "new", sol::no_constructor,
        "instance", &MessageView::instance,
        "put", [](MessageView* self, const char* msg){ self->put(msg); },
        "putln", [](MessageView* self, const char* msg){ self->putln(msg); },
        "notify", [](MessageView* self, const char* msg){ self->notify(msg); },
        "flush", &MessageView::flush,
        "clear", &MessageView::clear
        );

    module.new_usertype<TaskView>(
        "TaskView",
        sol::base_classes, sol::bases<View, QWidget, QObject, AbstractTaskSequencer>(),
        "new", sol::no_constructor,
        "instance", &TaskView::instance
        );

    module.new_usertype<Item>(
        "Item",
        "new", sol::no_constructor,
        "cast", [](Item* item) -> ItemPtr { return item; },
        "find", [](const char* path) -> ItemPtr { return Item::find(path); },
        "name", &Item::name,
        "setName", &Item::setName,
        "addChildItem", [](Item* self, Item* item) { return self->addChildItem(item); },
        "addSubItem", &Item::addSubItem,
        "isSubItem", &Item::isSubItem,
        "detachFromParentItem", &Item::detachFromParentItem,
        "isTemporal", &Item::isTemporal,
        "setTemporal", &Item::setTemporal,
        "notifyUpdate", &Item::notifyUpdate,

        "getDescendantItems", [](Item* self, sol::table itemClass, sol::this_state s){
            ItemList<Item> items;
            items.extractChildItems(self);
            sol::state_view lua(s);
            sol::table matched = lua.create_table();
            int index = 1;
            for(size_t i=0; i < items.size(); ++i){
                sol::object casted = itemClass["cast"](items[i]);
                if(casted != sol::nil){
                    matched[index++] = casted;
                }
            }
            return matched;
        }
        );

    module.new_usertype<RootItem>(
        "RootItem",
        sol::base_classes, sol::bases<Item>(),
        "new", sol::no_constructor,
        "instance", []() -> RootItemPtr { return RootItem::instance(); }
        );
    
    module.new_usertype<ExtCommandItem>(
        "ExtCommandItem",
        sol::base_classes, sol::bases<Item>(),
        "new", sol::factories([]() -> ExtCommandItemPtr { return new ExtCommandItem(); }),
        "cast", [](Item* item) -> ExtCommandItemPtr { return dynamic_cast<ExtCommandItem*>(item); },
        "setCommand", &ExtCommandItem::setCommand,
        "command", &ExtCommandItem::command,
        "waitingTimeAfterStarted", &ExtCommandItem::waitingTimeAfterStarted,
        "setWaitingTimeAfterStarted", &ExtCommandItem::setWaitingTimeAfterStarted,
        "execute", &ExtCommandItem::execute,
        "terminate", &ExtCommandItem::terminate
        );

    sol::stack::push(L, module);
    
    return 1;
}
