#include "shoptemplatemanager.h"
#include "buyoutmanager.h"

#include "external/boolinq.h"
#include <QStringList>
#include <QRegularExpression>
#include <QDebug>
#include <QDateTime>

using namespace boolinq;
const int POE_MAX_CHAR_IN_POST = 50000;

ShopTemplateManager::ShopTemplateManager(Application *parent)
    : QObject(parent)
    , parent_(parent) {
    LoadTemplateMatchers();
}

QList<ShopTemplateManager::ShopTemplateSection> ShopTemplateManager::FetchFromEasyKey(const QString &key, QHash<QString, QString>* options) {
    QList<ShopTemplateSection> result;
    if (key == "ign") {
        result << ShopTemplateSection(QString::fromStdString(parent_->active_character()));
    }
    else if (key == "lastupdated") {
        result << ShopTemplateSection(QDateTime::currentDateTime().toString("MMMM dd, yyyy hh:mm A"));
    }

    return result;
}

QStringList ShopTemplateManager::WriteItems(const Items &items, bool includeBuyoutTag, bool includeNoBuyouts) {
    Items sorted = items;
    QStringList result;

    // Sort to ensure that the template matches even if the item order is different.
    std::sort(sorted.begin(), sorted.end(), [](const std::shared_ptr<Item> &first, const std::shared_ptr<Item> &second) -> bool {
        return first->PrettyName() > second->PrettyName();
    });

    for (auto &item : sorted) {
        if (item->location().socketed())
            continue;

        if (parent_->buyout_manager().Exists(*item)) {
            Buyout bo = parent_->buyout_manager().Get(*item);

            QString temp = QString::fromStdString(item->location().GetForumCode(parent_->league()));
            if (includeBuyoutTag)
                temp += BuyoutManager::Generate(bo);
            result << temp;
        }
        else if (includeNoBuyouts) {
            result << QString::fromStdString(item->location().GetForumCode(parent_->league()));
        }
    }
    return result;
}

QList<ShopTemplateManager::ShopTemplateSection> ShopTemplateManager::FetchFromItemsKey(const QString &key, const Items &items, QHash<QString, QString> *options) {
    QList<ShopTemplateManager::ShopTemplateSection> result;
    if (items.size() > 0){
        Items matchingItems;

        bool includeNoBuyouts = options->contains("include.ignored");
        ShopTemplateContainType containType = CONTAIN_TYPE_NONE;

        if (key == "everything") {
            matchingItems = items;
        }
        else if (key.startsWith("stash:")) {
            int index = key.indexOf(":");
            QString name = (index + 1 >= key.length()) ? "" : key.mid(index + 1);
            if (!name.isEmpty()) {
                for (const std::shared_ptr<Item> &item : items) {
                    if (QString::fromStdString(item->location().GetLabel()).compare(name, Qt::CaseInsensitive) == 0) {
                        matchingItems.push_back(item);
                    }
                }
            }
        }
        else {
            Items pool = items;
            QStringList keyParts = key.split("+", QString::SkipEmptyParts);
            for (QString part : keyParts) {
                if (templateMatchers.contains(part)) {
                    matchingItems = FindMatchingItems(pool, part);
                }
                else {
                    // todo(novynn): phase these out? Prefer {Normal+Helmet} over {NormalHelmet}
                    bool matchedRarity = false;
                    const QStringList rarities = {"normal", "magic", "rare", "unique"};
                    for (QString rarity : rarities) {
                        if (part.startsWith(rarity)) {
                            QString type = part.mid(rarity.length());

                            matchingItems = FindMatchingItems(pool, rarity);
                            matchingItems = FindMatchingItems(matchingItems, type);
                            matchedRarity = true;
                        }
                    }

                    if (matchedRarity) continue;

                    if (part.endsWith("gems")) {
                        if (part == "gems" || part == "allgems") {
                            matchingItems = FindMatchingItems(pool, "gems");
                        }
                        else {
                            const QStringList gemTypes = {"AoE", "Attack", "Aura", "Bow", "Cast", "Chaining", "Chaos",
                                                          "Cold", "Curse", "Duration", "Fire", "Lightning", "Melee", "Mine", "Minion",
                                                          "Movement", "Projectile", "Spell", "Totem", "Trap", "Support"};
                            for (QString gemType : gemTypes) {
                                if (!part.startsWith(gemType, Qt::CaseInsensitive)) continue;

                                // This will never just be first key (?)
                                QString firstKey = gemType.toLower() + "gems";
                                matchingItems = FindMatchingItems(pool, firstKey);

                                if (part != firstKey) {
                                    // supportlightninggems
                                    QString secondKey = part.mid(gemType.length());
                                    matchingItems = FindMatchingItems(matchingItems, secondKey);
                                }
                            }
                        }
                    }
                }
                pool = matchingItems;
            }
        }

        // Only select items from your stash unless specified
        if (!options->contains("include.character")) {
            matchingItems = Items::fromStdVector(
                        from(matchingItems.toStdVector())
                        .where([](const std::shared_ptr<Item> item) { return item->location().type() == ItemLocationType::STASH; })
                        .toVector()
                     );
        }

        if (matchingItems.size() == 0)
            return result;

        if (containType == CONTAIN_TYPE_NONE && options->contains("wrap")) containType = CONTAIN_TYPE_WRAP;
        if (containType == CONTAIN_TYPE_NONE && options->contains("group")) containType = CONTAIN_TYPE_GROUP;

        switch (containType) {
            case (CONTAIN_TYPE_WRAP): {
                QString header = "Items";
                Buyout buyout = {};

                if (options->contains("header")) {
                    header = options->value("header");
                }

                const std::shared_ptr<Item> first = matchingItems.first();
                if (parent_->buyout_manager().Exists(*first)) buyout = parent_->buyout_manager().Get(*first);

                bool sameBuyout = from(matchingItems.toStdVector()).all([this, buyout](const std::shared_ptr<Item> item) {
                    Buyout thisBuyout = {};
                    if (parent_->buyout_manager().Exists(*item)) thisBuyout = parent_->buyout_manager().Get(*item);
                    return BuyoutManager::Equal(thisBuyout, buyout);
                });

                if (sameBuyout) {
                    header += BuyoutManager::Generate(buyout);
                }

                QStringList temp = WriteItems(matchingItems, !sameBuyout, includeNoBuyouts);
                if (!temp.isEmpty()) {
                    result << ShopTemplateSection(temp, SECTION_TYPE_SPOILER, header);
                }
                break;
            }
            case CONTAIN_TYPE_GROUP: {
                QMultiMap<QString, std::shared_ptr<Item>> itemsMap;

                for (auto &item : matchingItems) {
                    Buyout b = {};
                    if (parent_->buyout_manager().Exists(*item))
                        b = parent_->buyout_manager().Get(*item);
                    itemsMap.insert(BuyoutManager::Generate(b), item);
                }

                for (QString buyout : itemsMap.uniqueKeys()) {
                    Items itemList = itemsMap.values(buyout).toVector();
                    if (itemList.size() == 0)
                        continue;
                    QString header = buyout;
                    if (header.isEmpty()) header = "Offers Accepted";
                    QStringList temp = WriteItems(itemList, false, includeNoBuyouts);
                    if (temp.isEmpty())
                        continue;

                    result << ShopTemplateSection(temp, SECTION_TYPE_SPOILER, header);
                }
                break;
            }
            default: {
                QStringList temp = WriteItems(matchingItems, true, includeNoBuyouts);
                result << ShopTemplateSection(temp, SECTION_TYPE_NONE);
                break;
            }
        }
    }

    return result;
}

QList<ShopTemplateManager::ShopTemplateSection> ShopTemplateManager::FetchFromKey(const QString &key, const Items &items, QHash<QString, QString>* options) {
    QList<ShopTemplateManager::ShopTemplateSection> result = FetchFromEasyKey(key, options);

    if (result.isEmpty()) {
        result = FetchFromItemsKey(key, items, options);
    }

    return result;
}

// The actual generation engine
QStringList ShopTemplateManager::Generate(const Items &items) {
    QStringList result;
    QString temp = shopTemplate;

    // Records extractor
    QList<ShopTemplateRecord> records;
    {
        QRegularExpression expr("{(?<key>.+?)(?<options>(\\|(.+?))*?)}");
        QRegularExpressionMatchIterator matcher = expr.globalMatch(shopTemplate);
        int offset = 0;
        while (matcher.hasNext()) {
            QRegularExpressionMatch match = matcher.next();
            QString key = match.captured("key").toLower();
            int startPos = offset + match.capturedStart();
            const int len = match.capturedLength();

            QStringList optionsAndData = match.captured("options").split("|", QString::SkipEmptyParts);
            QHash<QString, QString> options;
            for (QString optionAndData : optionsAndData) {
                int split = optionAndData.indexOf(":");
                if (split == -1) {
                    options.insert(optionAndData.toLower(), "");
                }
                else {
                    QString option = optionAndData.left(split).toLower();
                    QString data = optionAndData.mid(split + 1);
                    options.insert(option, data);
                }
            }
            temp.remove(startPos, len);
            ShopTemplateRecord record = {key, options, startPos};
            records << record;
            offset -= len;
        }
    }

    const QString cleanTemplate = temp;

    // Injector
    {
        int indexOffset = 0;
        for (ShopTemplateRecord record : records){
            QList<ShopTemplateSection> sections = FetchFromKey(record.key, items, &record.options);
            int templateOffset = record.templateIndex;
            while (!sections.isEmpty()) {
                ShopTemplateSection replacement = sections.takeFirst();
                int itemStart = 0;
                int itemCount = replacement.items();
                while (itemCount > 0) {
                    QString text = replacement.generate(itemStart, itemCount);
                    if (temp.size() + text.size() >= POE_MAX_CHAR_IN_POST) {
                        itemCount--;
                        continue;
                    }
                    temp.insert(templateOffset + indexOffset, text);
                    templateOffset += text.size();

                    // If we ended up ignoring some items
                    if (itemStart + itemCount < replacement.items()) {
                        // We couldn't fit in all the items from last time into the chunk...
                        result << temp;
                        // New temp starts from the old templates end
                        temp = cleanTemplate.mid(record.templateIndex);
                        indexOffset = -record.templateIndex;
                        templateOffset = 0;
                        itemStart += itemCount;
                        itemCount = replacement.items() - itemStart;
                    }
                    else {
                        break;
                    }
                }
            }
            indexOffset += (templateOffset - record.templateIndex);
        }
        if (!temp.isEmpty())
            result << temp;
    }

    return result;
}

Items ShopTemplateManager::FindMatchingItems(const Items &items, QString keyword) {
    Items matched;
    for (auto &item : items) {
        if (IsMatchingItem(item, keyword)) {
            matched.push_back(item);
        }
    }
    return matched;
}


bool ShopTemplateManager::IsMatchingItem(const std::shared_ptr<Item> &item, QString keyword) {
    QString key = keyword.toLower();
    if (templateMatchers.contains(key)) {
        auto func = templateMatchers.value(key);
        return func(item);
    }
    return false;
}

void ShopTemplateManager::LoadTemplateMatchers() {
    templateMatchers.insert("droponlygems", [](const std::shared_ptr<Item> &item) {
        // - This ones from the wiki(http://pathofexile.gamepedia.com/Drop_Only_Gems). This will exclude quality drop only gems
        const QStringList dropOnly = {"Added Chaos Damage", "Detonate Mines",
                                      "Empower", "Enhance", "Enlighten", "Portal"};
        QString name = QString::fromStdString(item->typeLine());
        return (item->frameType() == FRAME_TYPE_GEM &&
                (dropOnly.contains(name) ||
                 name.startsWith("Vaal")) &&
                item->properties().find("Quality") == item->properties().end());
    });

    for (int i = 1; i <= 20; i++) {
        templateMatchers.insert("quality" + QString::number(i) + "gems", [i](const std::shared_ptr<Item> &item) {
            auto quality = item->properties().find("Quality");
            QString q = QString::fromStdString(quality->second);
            return item->frameType() == FRAME_TYPE_GEM && !q.isEmpty() && q == QString("+%1%").arg(i);
        });
    }

    for (int i = 1; i <= 20; i++) {
        templateMatchers.insert("level" + QString::number(i) + "gems", [i](const std::shared_ptr<Item> &item) {
            auto level = item->properties().find("Level");
            int l = QString::fromStdString(level->second).toInt();
            return item->frameType() == FRAME_TYPE_GEM && l == i;
        });
    }
    templateMatchers.insert("leveledgems", [](const std::shared_ptr<Item> &item) {
        auto level = item->properties().find("Level");
        int l = QString::fromStdString(level->second).toInt();
        return item->frameType() == FRAME_TYPE_GEM && l > 1;
    });

    templateMatchers.insert("corruptedgems", [](const std::shared_ptr<Item> &item) {
        return item->frameType() == FRAME_TYPE_GEM && item->corrupted();
    });

    templateMatchers.insert("gems", [](const std::shared_ptr<Item> &item) {
        return item->frameType() == FRAME_TYPE_GEM;
    });

    //    ???{PopularGems} - This is based on the Popular Gems list in settings.xml

    //    {MagicFind} - Any item with rarity, quantity or both stats will be included
    //    {DualRes} - Any item with 2 explicit resist stats
    //    {TripRes} - Any item with 3 or more explicit resist stats
    //    {OneHanders} - All one handed weapons
    //    {TwoHanders} - All two handed weapons
    //    {Life} - All items with +to maximum Life
    //    {LifeRegen} - All items with Life Regenerated per second
    //    {CritChance} - All items with crit chance
    //    {GlobalCritChance} - All items with global crit chance
    //    {GlobalCritMultiplier} - All items with global crit multiplier
    //    {SpellDamage} - All items with spell damage
    //    {ManaRegen} - Items with added mana regen
    //    {PhysicalDamage} - Items with added physical damage
    //    {IncreasedPhysicalDamage} - Items with Increased Physical Damage
    //    {EnergyShield} - Items with increased Energy Shield

    templateMatchers.insert("vaalfragments", [](const std::shared_ptr<Item> &item) {
        QStringList fragments = {"Sacrifice at Dusk", "Sacrifice at Midnight", "Sacrifice at Noon", "Sacrifice at Dawn"};
        return fragments.contains(QString::fromStdString(item->typeLine()));
    });

    templateMatchers.insert("vaaluberfragments", [](const std::shared_ptr<Item> &item) {
        QStringList fragments = {"Mortal Grief", "Mortal Rage", "Mortal Hope", "Mortal Ignorance"};
        return fragments.contains(QString::fromStdString(item->typeLine()));
    });

    //    {LvlMaps} - The keyword currently accepts a value for Lvl between 66 - 100. Some examples would be: {66Maps}, {67Maps}, {68Maps}
    for (int i = 68; i < 83; i++) {
        templateMatchers.insert(QString::number(i) + "maps", [i](const std::shared_ptr<Item> &item) {
            bool isMap = QString::fromStdString(item->typeLine()).contains("Map");
            if (!isMap) return false;
            int level = 0;
            for (auto &property : item->text_properties()) {
                if (property.name != "Level") continue;
                level = QString::fromStdString(QVector<std::string>::fromStdVector(property.values).first()).toInt();

            }
            return level == i;
        });
    }

    for (int i = 4; i < 7; i++) {
        templateMatchers.insert(QString::number(i) + "link", [i](const std::shared_ptr<Item> &item) {
            return item->links_cnt() == i;
        });
    }

    templateMatchers.insert("6socket", [](const std::shared_ptr<Item> &item) {
        return item->sockets_cnt() == 6;
    });

    templateMatchers.insert("normal", [](const std::shared_ptr<Item> &item) {
        return item->frameType() == FRAME_TYPE_NORMAL;
    });
    templateMatchers.insert("normalgear", [](const std::shared_ptr<Item> &item) {
        return item->frameType() == FRAME_TYPE_NORMAL;
    });
    templateMatchers.insert("magic", [](const std::shared_ptr<Item> &item) {
        return item->frameType() == FRAME_TYPE_MAGIC;
    });
    templateMatchers.insert("rare", [](const std::shared_ptr<Item> &item) {
        return item->frameType() == FRAME_TYPE_RARE;
    });
    templateMatchers.insert("unique", [](const std::shared_ptr<Item> &item) {
        return item->frameType() == FRAME_TYPE_UNIQUE;
    });
    templateMatchers.insert("uniques", [](const std::shared_ptr<Item> &item) {
        return item->frameType() == FRAME_TYPE_UNIQUE;
    });

    QMap<QString, QStringList> typeMap;
    typeMap.insert("ring", {"Iron Ring", "Coral Ring", "Paua Ring", "Gold Ring",
                            "Ruby Ring", "Sapphire Ring", "Topaz Ring", "Diamond Ring",
                            "Moonstone Ring", "Prismatic Ring", "Amethyst Ring",
                            "Two-Stone Ring", "Unset Ring"});

    // todo(novynn): use some sort of external definitions system that we can just load in?

    // Ring, Amulet, Helmet, Chest, Belt, Gloves, Boots, Axe, Claw, Bow, Dagger, Mace, Quiver, Sceptre, Staff, Sword, Shield, Wand, Flask

    const QStringList gemTypes = {"AoE", "Attack", "Aura", "Bow", "Cast", "Chaining", "Chaos",
                                  "Cold", "Curse", "Duration", "Fire", "Lightning", "Melee", "Mine", "Minion",
                                  "Movement", "Projectile", "Spell", "Totem", "Trap", "Support"};

    for (QString gemType : gemTypes) {
        QString key = gemType.toLower() + "gems";
        templateMatchers.insert(key, [gemType](const std::shared_ptr<Item> &item) {
            if (item->frameType() != FRAME_TYPE_GEM) return false;
            // Is it safe to assume that the gem types will always be the top of the properties list?
            auto types = item->text_properties()[0];
            return QString::fromStdString(types.name).contains(gemType);
        });
    }

}



ShopTemplateManager::ShopTemplateSection::ShopTemplateSection(QStringList contents, ShopTemplateManager::ShopTemplateSectionType type, QString name)
    : contents(contents)
    , type(type)
    , header()
    , footer() {
    if (type == SECTION_TYPE_SPOILER) {
        header = "[spoiler=\"" + name + "\"]";
        footer = "[/spoiler]";
    }
}

ShopTemplateManager::ShopTemplateSection::ShopTemplateSection(QString content, ShopTemplateManager::ShopTemplateSectionType type, QString name)
    : ShopTemplateSection(QStringList({content}), type, name) {
}


QString ShopTemplateManager::ShopTemplateSection::generate(int start, int items) {
    return header + spliced(start, items).join("") + footer;
}

int ShopTemplateManager::ShopTemplateSection::size(int start, int items) {
    return header.length() + spliced(start, items).join("").length() + footer.length();
}

bool ShopTemplateManager::ShopTemplateSection::isEmpty() {
    return contents.isEmpty();
}

ShopTemplateManager::ShopTemplateSectionType ShopTemplateManager::ShopTemplateSection::getType() {
    return type;
}
