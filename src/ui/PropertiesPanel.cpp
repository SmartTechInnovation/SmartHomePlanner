#include "PropertiesPanel.h"

#include "core/Geometry.h"
#include "core/IconCache.h"
#include "core/Library.h"
#include "scene/DeviceItem.h"
#include "scene/EffectItem.h"
#include "scene/GroupItem.h"
#include "scene/PanelItems.h"
#include "scene/PlanScene.h"
#include "scene/RoomItem.h"
#include "scene/StripItem.h"
#include "scene/WallItem.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QDoubleSpinBox *spin(double min, double max, double value, const QString &suffix, int decimals = 0, double step = 1)
{
    auto *s = new QDoubleSpinBox;
    s->setRange(min, max);
    s->setDecimals(decimals);
    s->setSingleStep(step);
    s->setValue(value);
    s->setSuffix(suffix);
    s->setKeyboardTracking(false);
    s->setAccelerated(true);
    return s;
}

QPushButton *colorButton(const QColor &c)
{
    auto *b = new QPushButton;
    b->setMinimumWidth(80);
    b->setStyleSheet(QStringLiteral("background:%1; border:1px solid #888; min-height:20px;").arg(c.name()));
    b->setText(c.name().toUpper());
    return b;
}

void openLink(const QString &text)
{
    if (text.trimmed().isEmpty())
        return;
    QDesktopServices::openUrl(QUrl::fromUserInput(text.trimmed()));
}

const int kKelvins[] = {2700, 3000, 3500, 4000, 5000, 6500};

} // namespace

PropertiesPanel::PropertiesPanel(PlanScene *scene, QWidget *parent) : QScrollArea(parent)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setMinimumWidth(270);
    setScene(scene);
}

void PropertiesPanel::setScene(PlanScene *scene)
{
    if (m_scene) {
        disconnect(m_scene, nullptr, this, nullptr);
        disconnect(m_undoConn);
    }
    m_scene = scene;
    connect(scene, &QGraphicsScene::selectionChanged, this, &PropertiesPanel::scheduleRebuild);
    connect(scene, &PlanScene::itemEdited, this, [this] {
        if (!m_applying)
            scheduleRebuild();
    });
    m_undoConn = connect(scene->undoStack(), &QUndoStack::indexChanged, this, [this] {
        if (!m_applying)
            scheduleRebuild();
    });
    // the lists may point to items of the previous floor
    m_devices.clear();
    m_strips.clear();
    m_walls.clear();
    m_rooms.clear();
    m_groups.clear();
    m_equipment.clear();
    m_wires.clear();
    m_rails.clear();
    rebuild();
}

void PropertiesPanel::scheduleRebuild()
{
    // never delete the editor widgets from inside one of their own signals
    if (m_rebuildPending)
        return;
    m_rebuildPending = true;
    QTimer::singleShot(0, this, [this] {
        m_rebuildPending = false;
        rebuild();
    });
}

void PropertiesPanel::rebuild()
{
    m_devices.clear();
    m_strips.clear();
    m_walls.clear();
    m_rooms.clear();
    m_groups.clear();
    m_equipment.clear();
    m_wires.clear();
    m_rails.clear();
    for (QGraphicsItem *it : m_scene->selectedItems()) {
        switch (it->type()) {
        case ItemType::Group:     m_groups << static_cast<GroupItem *>(it); break;
        case ItemType::Equipment: m_equipment << static_cast<EquipmentItem *>(it); break;
        case ItemType::Wire:      m_wires << static_cast<WireItem *>(it); break;
        case ItemType::Rail:      m_rails << static_cast<RailItem *>(it); break;
        case ItemType::Device: m_devices << static_cast<DeviceItem *>(it); break;
        case ItemType::Strip:  m_strips << static_cast<StripItem *>(it); break;
        case ItemType::Wall:   m_walls << static_cast<WallItem *>(it); break;
        case ItemType::Room:   m_rooms << static_cast<RoomItem *>(it); break;
        default: break;
        }
    }

    // keep the scroll position when the same selection is rebuilt
    const int scroll = verticalScrollBar() ? verticalScrollBar()->value() : 0;
    auto *content = new QWidget;
    auto *outer = new QVBoxLayout(content);
    outer->setContentsMargins(8, 8, 8, 8);
    m_form = new QFormLayout;
    m_form->setLabelAlignment(Qt::AlignRight);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    outer->addLayout(m_form);
    outer->addStretch(1);

    if (!m_equipment.isEmpty())
        buildEquipment();
    else if (!m_wires.isEmpty())
        buildWires();
    else if (!m_rails.isEmpty())
        buildRails();
    else if (!m_groups.isEmpty() && m_devices.isEmpty() && m_strips.isEmpty())
        buildGroups();
    else if (!m_devices.isEmpty())
        buildDevices();
    else if (!m_strips.isEmpty())
        buildStrips();
    else if (!m_rooms.isEmpty())
        buildRooms();
    else if (!m_walls.isEmpty())
        buildWalls();
    else
        buildProject();

    setWidget(content);     // deletes the previous content widget
    m_content = content;
    if (verticalScrollBar())
        verticalScrollBar()->setValue(scroll);
}

void PropertiesPanel::addHeader(const QString &title, const QString &subtitle, const QString &iconPath)
{
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 6);
    if (!iconPath.isEmpty()) {
        auto *icon = new QLabel;
        icon->setPixmap(IconCache::instance().icon(iconPath).pixmap(40, 40));
        h->addWidget(icon);
    }
    auto *txt = new QLabel(QStringLiteral("<b>%1</b><br><span style='color:#666'>%2</span>")
                               .arg(title.toHtmlEscaped(), subtitle.toHtmlEscaped()));
    txt->setWordWrap(true);
    h->addWidget(txt, 1);
    m_form->addRow(w);
}

void PropertiesPanel::addSeparator(const QString &title)
{
    auto *l = new QLabel(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()));
    l->setContentsMargins(0, 10, 0, 2);
    m_form->addRow(l);
    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    m_form->addRow(line);
}

void PropertiesPanel::editData(const QString &label, const std::function<void(DeviceData &)> &fn)
{
    m_applying = true;
    for (DeviceItem *d : m_devices) {
        fn(d->dataRef());
        d->refresh();
    }
    for (StripItem *s : m_strips) {
        fn(s->dataRef());
        s->refresh();
    }
    for (EquipmentItem *e : m_equipment) {
        fn(e->dataRef());
        e->update();
    }
    m_scene->commit(label);
    m_applying = false;
}

void PropertiesPanel::editDevices(const QString &label, const std::function<void(DeviceItem *)> &fn)
{
    m_applying = true;
    for (DeviceItem *d : m_devices) {
        fn(d);
        d->refresh();
    }
    m_scene->commit(label);
    m_applying = false;
}

void PropertiesPanel::editStrips(const QString &label, const std::function<void(StripItem *)> &fn)
{
    m_applying = true;
    for (StripItem *s : m_strips) {
        fn(s);
        s->refresh();
    }
    m_scene->commit(label);
    m_applying = false;
}

// ---------------------------------------------------------------- common (devices and strips)

void PropertiesPanel::buildCommon(const DeviceData &d, bool single)
{
    if (single) {
        auto *id = new QLineEdit(d.id);
        id->setToolTip(tr("Identifier printed on the plan and in the PDF legend"));
        connect(id, &QLineEdit::editingFinished, this, [this, id] {
            editData(tr("Change ID"), [&](DeviceData &x) { x.id = id->text().trimmed(); });
        });
        m_form->addRow(tr("ID"), id);
    }

    auto *section = new QComboBox;
    for (const SectionDef &s : Library::instance().sections())
        section->addItem(s.name, s.id);
    section->setCurrentIndex(section->findData(d.section));
    connect(section, &QComboBox::currentIndexChanged, this, [this, section] {
        const QString sid = section->currentData().toString();
        editData(tr("Change chapter"), [&](DeviceData &x) { x.section = sid; });
    });
    m_form->addRow(tr("Chapter"), section);

    auto *note = new QLineEdit(d.note);
    note->setPlaceholderText(tr("e.g. 3x1.5, Tree, Cat6"));
    note->setToolTip(tr("Shown next to the element on the plan (cable type, bus ...)"));
    connect(note, &QLineEdit::editingFinished, this, [this, note] {
        editData(tr("Change note"), [&](DeviceData &x) { x.note = note->text(); });
    });
    m_form->addRow(tr("Note"), note);

    auto *showNote = new QCheckBox(tr("Show note on the plan"));
    showNote->setChecked(d.showNote);
    connect(showNote, &QCheckBox::toggled, this, [this](bool on) {
        editData(tr("Show note"), [&](DeviceData &x) { x.showNote = on; });
    });
    m_form->addRow(QString(), showNote);

    // documentation
    auto *docRow = new QWidget;
    auto *docLay = new QHBoxLayout(docRow);
    docLay->setContentsMargins(0, 0, 0, 0);
    auto *doc = new QLineEdit(d.doc);
    doc->setPlaceholderText(tr("URL or file"));
    auto *docBrowse = new QToolButton;
    docBrowse->setText(QStringLiteral("…"));
    docBrowse->setToolTip(tr("Choose a file"));
    auto *docOpen = new QToolButton;
    docOpen->setText(tr("Open"));
    docLay->addWidget(doc, 1);
    docLay->addWidget(docBrowse);
    docLay->addWidget(docOpen);
    connect(doc, &QLineEdit::editingFinished, this, [this, doc] {
        editData(tr("Change documentation"), [&](DeviceData &x) { x.doc = doc->text().trimmed(); });
    });
    connect(docBrowse, &QToolButton::clicked, this, [this, doc] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Documentation file"), QString(),
                                                       tr("Documents (*.pdf *.doc *.docx *.txt *.html);;All files (*)"));
        if (f.isEmpty())
            return;
        doc->setText(QUrl::fromLocalFile(f).toString());
        editData(tr("Change documentation"), [&](DeviceData &x) { x.doc = doc->text(); });
    });
    connect(docOpen, &QToolButton::clicked, this, [doc] { openLink(doc->text()); });
    m_form->addRow(tr("Documentation"), docRow);

    auto *shopRow = new QWidget;
    auto *shopLay = new QHBoxLayout(shopRow);
    shopLay->setContentsMargins(0, 0, 0, 0);
    auto *shop = new QLineEdit(d.shop);
    shop->setPlaceholderText(QStringLiteral("https://"));
    auto *shopOpen = new QToolButton;
    shopOpen->setText(tr("Open"));
    shopLay->addWidget(shop, 1);
    shopLay->addWidget(shopOpen);
    connect(shop, &QLineEdit::editingFinished, this, [this, shop] {
        editData(tr("Change shop link"), [&](DeviceData &x) { x.shop = shop->text().trimmed(); });
    });
    connect(shopOpen, &QToolButton::clicked, this, [shop] { openLink(shop->text()); });
    m_form->addRow(tr("Shop link"), shopRow);
}

// ---------------------------------------------------------------- devices

void PropertiesPanel::buildDevices()
{
    DeviceItem *first = m_devices.first();
    const DeviceData &d = first->data();
    const bool single = m_devices.size() == 1 && m_strips.isEmpty();
    addHeader(single ? d.displayName() : tr("%n elements selected", nullptr, m_devices.size() + m_strips.size()),
              single ? Library::instance().sectionName(d.section) : tr("Changes apply to all selected"),
              single ? d.iconPath : QString());

    buildCommon(d, single);
    if (single)
        addGroupInfo(d.uid);

    addSeparator(tr("Symbol"));
    auto *size = spin(5, 500, d.size, tr(" cm"));
    connect(size, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        editData(tr("Symbol size"), [&](DeviceData &x) { x.size = v; });
    });
    m_form->addRow(tr("Size"), size);

    auto *rot = spin(-360, 360, d.rotation, QStringLiteral("°"), 0, 15);
    connect(rot, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        editData(tr("Rotate"), [&](DeviceData &x) { x.rotation = v; });
    });
    m_form->addRow(tr("Rotation"), rot);

    auto *tile = new QCheckBox(tr("White background"));
    tile->setToolTip(tr("White rounded background behind the symbol – for dark floor plans or symbols on walls.\n"
                        "Default from the library (tile=\"1\" / \"0\" in the XML; SVG symbols: on)."));
    bool allOn = true, allOff = true;
    for (DeviceItem *x : m_devices) {
        allOn &= x->data().tile;
        allOff &= !x->data().tile;
    }
    tile->setTristate(!allOn && !allOff);
    tile->setCheckState(allOn ? Qt::Checked : allOff ? Qt::Unchecked : Qt::PartiallyChecked);
    connect(tile, &QCheckBox::clicked, this, [this, tile](bool on) {
        tile->setTristate(false);
        tile->setChecked(on);
        editDevices(tr("White background"), [on](DeviceItem *d) { d->dataRef().tile = on; });
    });
    m_form->addRow(QString(), tile);

    const bool allLights = std::all_of(m_devices.begin(), m_devices.end(), [](DeviceItem *x) { return x->isLight(); });
    const bool allSpeakers = std::all_of(m_devices.begin(), m_devices.end(), [](DeviceItem *x) { return x->isSpeaker(); });
    const bool allWifi = std::all_of(m_devices.begin(), m_devices.end(), [](DeviceItem *x) { return x->isWifi(); });

    if (allLights) {
        addSeparator(tr("Light"));
        auto *on = new QCheckBox(tr("On (double-click on the plan)"));
        on->setChecked(d.on);
        connect(on, &QCheckBox::toggled, this, [this](bool v) {
            editDevices(tr("Switch light"), [&](DeviceItem *x) { x->setOn(v); });
        });
        m_form->addRow(QString(), on);

        auto *diameter = new QLabel;
        auto updateDiameter = [first, diameter] {
            diameter->setText(Geo::formatMeters(2 * first->effectRadius()));
        };

        auto *beam = spin(1, 180, d.beam, QStringLiteral("°"));
        beam->setToolTip(tr("Beam angle of the luminaire"));
        connect(beam, &QDoubleSpinBox::valueChanged, this, [this, updateDiameter](double v) {
            editDevices(tr("Beam angle"), [&](DeviceItem *x) { x->dataRef().beam = v; });
            updateDiameter();
        });
        m_form->addRow(tr("Beam angle"), beam);

        auto *mount = spin(0, 2000, d.mountHeight, tr(" cm"));
        mount->setSpecialValueText(tr("Ceiling (%1 cm)").arg(m_scene->settings().ceilingHeight));
        mount->setToolTip(tr("Mounting height. 0 = project ceiling height"));
        connect(mount, &QDoubleSpinBox::valueChanged, this, [this, updateDiameter](double v) {
            editDevices(tr("Mounting height"), [&](DeviceItem *x) { x->dataRef().mountHeight = v; });
            updateDiameter();
        });
        m_form->addRow(tr("Mounting height"), mount);

        auto *colorRow = new QWidget;
        auto *colorLay = new QHBoxLayout(colorRow);
        colorLay->setContentsMargins(0, 0, 0, 0);
        auto *cct = new QComboBox;
        for (int k : kKelvins)
            cct->addItem(QStringLiteral("%1 K").arg(k), k);
        cct->addItem(tr("Color (RGB)"), 0);
        if (d.rgb.isValid()) {
            cct->setCurrentIndex(cct->count() - 1);
        } else {
            int idx = cct->findData(d.cct);
            if (idx < 0) {
                cct->insertItem(0, QStringLiteral("%1 K").arg(d.cct), d.cct);
                idx = 0;
            }
            cct->setCurrentIndex(idx);
        }
        auto *rgbBtn = colorButton(d.rgb.isValid() ? d.rgb : first->lightColor());
        rgbBtn->setVisible(d.rgb.isValid());
        colorLay->addWidget(cct, 1);
        colorLay->addWidget(rgbBtn);
        connect(cct, &QComboBox::currentIndexChanged, this, [this, cct, rgbBtn] {
            const int k = cct->currentData().toInt();
            if (k > 0) {
                rgbBtn->setVisible(false);
                editDevices(tr("Light color"), [&](DeviceItem *x) {
                    x->dataRef().cct = k;
                    x->dataRef().rgb = QColor();
                });
            } else {
                rgbBtn->setVisible(true);
                editDevices(tr("Light color"), [&](DeviceItem *x) {
                    if (!x->dataRef().rgb.isValid())
                        x->dataRef().rgb = QColor(255, 80, 200);
                });
            }
        });
        connect(rgbBtn, &QPushButton::clicked, this, [this, first, rgbBtn] {
            const QColor c = QColorDialog::getColor(first->lightColor(), this, tr("Light color"));
            if (!c.isValid())
                return;
            rgbBtn->setStyleSheet(QStringLiteral("background:%1; border:1px solid #888; min-height:20px;").arg(c.name()));
            rgbBtn->setText(c.name().toUpper());
            editDevices(tr("Light color"), [&](DeviceItem *x) { x->dataRef().rgb = c; });
        });
        m_form->addRow(tr("Color"), colorRow);

        updateDiameter();
        diameter->setToolTip(tr("Diameter of the light spot on the calculation plane"));
        m_form->addRow(tr("Light spot Ø"), diameter);
    } else if (allWifi) {
        addSeparator(tr("Wi-Fi coverage"));
        auto *hint = new QLabel(tr("<i>The coverage is shown while the access point is selected or dragged.</i>"));
        hint->setWordWrap(true);
        m_form->addRow(hint);
        auto *on = new QCheckBox(tr("Show the coverage in the PDF"));
        on->setChecked(d.on);
        connect(on, &QCheckBox::toggled, this, [this](bool v) {
            editDevices(tr("Wi-Fi coverage"), [&](DeviceItem *x) { x->setOn(v); });
        });
        m_form->addRow(QString(), on);
        auto *range = spin(1, 100, d.radius / 100.0, tr(" m"), 1, 0.5);
        range->setToolTip(tr("Range without walls (open space)"));
        connect(range, &QDoubleSpinBox::valueChanged, this, [this](double v) {
            editDevices(tr("Wi-Fi range"), [&](DeviceItem *x) { x->dataRef().radius = v * 100.0; });
        });
        m_form->addRow(tr("Range"), range);
        auto *loss = spin(0, 100, d.wallLoss, QStringLiteral(" %"), 0, 5);
        loss->setToolTip(tr("Part of the range lost by every wall the signal goes through\n"
                            "(plasterboard ~15–20 %, brick ~30–40 %, concrete ~50–70 %)"));
        connect(loss, &QDoubleSpinBox::valueChanged, this, [this](double v) {
            editDevices(tr("Wi-Fi wall loss"), [&](DeviceItem *x) { x->dataRef().wallLoss = v; });
        });
        m_form->addRow(tr("Loss per wall"), loss);
        auto *legend = new QLabel(tr("<span style='color:#1EBE50'>&#9632;</span> excellent  "
                                     "<span style='color:#E6C800'>&#9632;</span> good  "
                                     "<span style='color:#FF8C00'>&#9632;</span> weak"));
        m_form->addRow(tr("Signal"), legend);
    } else if (allSpeakers) {
        addSeparator(tr("Speaker"));
        auto *on = new QCheckBox(tr("Playing (double-click on the plan)"));
        on->setChecked(d.on);
        connect(on, &QCheckBox::toggled, this, [this](bool v) {
            editDevices(tr("Switch speaker"), [&](DeviceItem *x) { x->setOn(v); });
        });
        m_form->addRow(QString(), on);
        auto *radius = spin(10, 5000, d.radius, tr(" cm"), 0, 10);
        connect(radius, &QDoubleSpinBox::valueChanged, this, [this](double v) {
            editDevices(tr("Speaker radius"), [&](DeviceItem *x) { x->dataRef().radius = v; });
        });
        m_form->addRow(tr("Coverage radius"), radius);
    }
}

// ---------------------------------------------------------------- strips

void PropertiesPanel::buildStrips()
{
    StripItem *first = m_strips.first();
    const DeviceData &d = first->data();
    const bool single = m_strips.size() == 1;
    addHeader(single ? d.displayName() : tr("%n LED strips selected", nullptr, m_strips.size()),
              single ? Library::instance().sectionName(d.section) : tr("Changes apply to all selected"),
              single ? d.iconPath : QString());
    buildCommon(d, single);
    if (single)
        addGroupInfo(d.uid);

    addSeparator(tr("LED strip"));
    auto *on = new QCheckBox(tr("On (double-click on the plan)"));
    on->setChecked(d.on);
    connect(on, &QCheckBox::toggled, this, [this](bool v) {
        editStrips(tr("Switch LED strip"), [&](StripItem *x) { x->setOn(v); });
    });
    m_form->addRow(QString(), on);

    auto *colBtn = colorButton(first->color());
    connect(colBtn, &QPushButton::clicked, this, [this, first, colBtn] {
        const QColor c = QColorDialog::getColor(first->color(), this, tr("LED color"));
        if (!c.isValid())
            return;
        colBtn->setStyleSheet(QStringLiteral("background:%1; border:1px solid #888; min-height:20px;").arg(c.name()));
        colBtn->setText(c.name().toUpper());
        editStrips(tr("LED color"), [&](StripItem *x) { x->dataRef().rgb = c; });
    });
    m_form->addRow(tr("Color"), colBtn);

    auto *glow = spin(1, 300, d.radius, tr(" cm"));
    glow->setToolTip(tr("Width of the light effect around the strip"));
    connect(glow, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        editStrips(tr("Glow width"), [&](StripItem *x) { x->dataRef().radius = v; });
    });
    m_form->addRow(tr("Glow width"), glow);

    if (single && first->mode() == StripItem::Circle) {
        auto *r = spin(5, 5000, first->circleRadius(), tr(" cm"));
        connect(r, &QDoubleSpinBox::valueChanged, this, [this](double v) {
            editStrips(tr("Circle radius"), [&](StripItem *x) { x->setCircleRadius(v); });
        });
        m_form->addRow(tr("Radius"), r);
    }
    double total = 0;
    for (StripItem *s : m_strips)
        total += s->length();
    m_form->addRow(tr("Length"), new QLabel(Geo::formatMeters(total)));
}

// ---------------------------------------------------------------- walls

void PropertiesPanel::buildWalls()
{
    addHeader(m_walls.size() == 1 ? tr("Wall") : tr("%n walls", nullptr, m_walls.size()),
              tr("Walls block light and sound"));
    auto *thick = spin(1, 200, m_walls.first()->thickness(), tr(" cm"));
    connect(thick, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        m_applying = true;
        for (WallItem *w : m_walls)
            w->setThickness(v);
        m_scene->commit(tr("Wall thickness"));
        m_applying = false;
    });
    m_form->addRow(tr("Thickness"), thick);

    if (m_walls.size() == 1) {
        auto *closed = new QCheckBox(tr("Closed contour"));
        closed->setChecked(m_walls.first()->isClosed());
        connect(closed, &QCheckBox::toggled, this, [this](bool v) {
            m_applying = true;
            m_walls.first()->setClosed(v);
            m_scene->commit(tr("Close wall"));
            m_applying = false;
        });
        m_form->addRow(QString(), closed);
    }
    double len = 0;
    for (WallItem *w : m_walls)
        len += w->length();
    m_form->addRow(tr("Length"), new QLabel(Geo::formatMeters(len)));
}

// ---------------------------------------------------------------- rooms

void PropertiesPanel::buildRooms()
{
    RoomItem *first = m_rooms.first();
    const bool single = m_rooms.size() == 1;
    addHeader(single ? (first->name().isEmpty() ? tr("Room") : first->name()) : tr("%n rooms", nullptr, m_rooms.size()),
              tr("Surface"));
    if (single) {
        auto *name = new QLineEdit(first->name());
        connect(name, &QLineEdit::editingFinished, this, [this, name] {
            m_applying = true;
            m_rooms.first()->setName(name->text());
            m_scene->commit(tr("Rename room"));
            m_applying = false;
        });
        m_form->addRow(tr("Name"), name);
    }
    auto *colBtn = colorButton(first->color());
    connect(colBtn, &QPushButton::clicked, this, [this, first, colBtn] {
        const QColor c = QColorDialog::getColor(first->color(), this, tr("Room color"));
        if (!c.isValid())
            return;
        colBtn->setStyleSheet(QStringLiteral("background:%1; border:1px solid #888; min-height:20px;").arg(c.name()));
        colBtn->setText(c.name().toUpper());
        m_applying = true;
        for (RoomItem *r : m_rooms)
            r->setColor(c);
        m_scene->commit(tr("Room color"));
        m_applying = false;
    });
    m_form->addRow(tr("Color"), colBtn);

    auto *showArea = new QCheckBox(tr("Show area on the plan"));
    showArea->setChecked(first->showArea());
    connect(showArea, &QCheckBox::toggled, this, [this](bool v) {
        m_applying = true;
        for (RoomItem *r : m_rooms)
            r->setShowArea(v);
        m_scene->commit(tr("Show area"));
        m_applying = false;
    });
    m_form->addRow(QString(), showArea);

    double area = 0, perim = 0;
    for (RoomItem *r : m_rooms) {
        area += r->area();
        perim += r->perimeter();
    }
    auto *areaLbl = new QLabel(QStringLiteral("<b>%1</b>").arg(Geo::formatArea(area)));
    m_form->addRow(tr("Area"), areaLbl);
    m_form->addRow(tr("Perimeter"), new QLabel(Geo::formatMeters(perim)));
}

// ---------------------------------------------------------------- project

void PropertiesPanel::buildProject()
{
    const ProjectInfo &info = m_scene->info();
    addHeader(info.name, info.client.isEmpty() ? tr("Project") : info.client);
    auto *infoBtn = new QPushButton(tr("Project information..."));
    connect(infoBtn, &QPushButton::clicked, this, &PropertiesPanel::editProjectInfo);
    m_form->addRow(infoBtn);

    addSeparator(tr("Plan settings"));
    ProjectSettings &s = m_scene->settings();

    auto *ceiling = spin(100, 2000, s.ceilingHeight, tr(" cm"));
    ceiling->setToolTip(tr("Default mounting height of the lights"));
    connect(ceiling, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        m_applying = true;
        m_scene->settings().ceilingHeight = v;
        m_scene->refreshAll();
        m_scene->commit(tr("Ceiling height"));
        m_applying = false;
    });
    m_form->addRow(tr("Ceiling height"), ceiling);

    auto *plane = spin(0, 300, s.workPlane, tr(" cm"));
    plane->setToolTip(tr("Height of the plane where the light spots are shown (0 = floor, 80 = desk)"));
    connect(plane, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        m_applying = true;
        m_scene->settings().workPlane = v;
        m_scene->refreshAll();
        m_scene->commit(tr("Calculation plane"));
        m_applying = false;
    });
    m_form->addRow(tr("Calculation plane"), plane);

    auto *sym = spin(0.2, 5, s.symbolScale, QStringLiteral(" ×"), 2, 0.1);
    connect(sym, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        m_applying = true;
        m_scene->settings().symbolScale = v;
        m_scene->refreshAll();
        m_scene->commit(tr("Symbol scale"));
        m_applying = false;
    });
    m_form->addRow(tr("Symbol scale"), sym);

    auto *wall = spin(1, 200, s.wallThickness, tr(" cm"));
    connect(wall, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        m_scene->settings().wallThickness = v;
    });
    m_form->addRow(tr("New wall thickness"), wall);

    auto *grid = spin(1, 200, s.gridSize, tr(" cm"));
    connect(grid, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        m_scene->settings().gridSize = v;
    });
    m_form->addRow(tr("Snap grid"), grid);

    addSeparator(tr("Summary"));
    double area = 0;
    for (RoomItem *r : m_scene->rooms())
        area += r->area();
    m_form->addRow(tr("Rooms"), new QLabel(QString::number(m_scene->rooms().size())));
    m_form->addRow(tr("Total area"), new QLabel(Geo::formatArea(area)));
    QHash<QString, int> counts;
    for (DeviceItem *d : m_scene->devices())
        counts[d->data().section]++;
    for (StripItem *st : m_scene->strips())
        counts[st->data().section]++;
    for (const SectionDef &sec : Library::instance().sections())
        if (counts.value(sec.id) > 0)
            m_form->addRow(sec.name, new QLabel(QString::number(counts.value(sec.id))));
}

// ---------------------------------------------------------------- groups

void PropertiesPanel::addGroupInfo(const QString &uid)
{
    const QList<GroupItem *> gs = m_scene->groupsOf(uid);
    auto *row = new QWidget;
    auto *v = new QVBoxLayout(row);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    for (GroupItem *g : gs) {
        auto *h = new QHBoxLayout;
        auto *lbl = new QLabel(QStringLiteral("<span style='color:%1'>&#9632;</span> %2")
                                   .arg(g->color().name(), g->name().toHtmlEscaped()));
        auto *out = new QToolButton;
        out->setText(tr("Remove"));
        out->setToolTip(tr("Take this element out of the group"));
        connect(out, &QToolButton::clicked, this, [this, g, uid] {
            m_scene->removeFromGroup(g, uid);
            scheduleRebuild();
        });
        h->addWidget(lbl, 1);
        h->addWidget(out);
        v->addLayout(h);
    }
    if (gs.isEmpty())
        v->addWidget(new QLabel(tr("<i>none</i> – select several elements and press Group (Ctrl+G)")));
    m_form->addRow(tr("Groups"), row);
}

void PropertiesPanel::buildGroups()
{
    GroupItem *g = m_groups.first();
    const bool single = m_groups.size() == 1;
    addHeader(single ? (g->name().isEmpty() ? tr("Group") : g->name()) : tr("%n groups", nullptr, m_groups.size()),
              tr("Elements linked by a dashed line"));
    if (single) {
        auto *name = new QLineEdit(g->name());
        connect(name, &QLineEdit::editingFinished, this, [this, name] {
            m_applying = true;
            m_groups.first()->setName(name->text().trimmed());
            m_scene->commit(tr("Rename group"));
            m_applying = false;
        });
        m_form->addRow(tr("Name"), name);
        auto *note = new QLineEdit(g->note());
        note->setPlaceholderText(tr("e.g. circuit, cable, breaker"));
        connect(note, &QLineEdit::editingFinished, this, [this, note] {
            m_applying = true;
            m_groups.first()->setNote(note->text());
            m_scene->commit(tr("Group note"));
            m_applying = false;
        });
        m_form->addRow(tr("Note"), note);
    }
    // color: quick palette + custom
    auto *colors = new QWidget;
    auto *cl = new QHBoxLayout(colors);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(3);
    auto applyColor = [this](const QColor &c) {
        m_applying = true;
        for (GroupItem *x : m_groups)
            x->setColor(c);
        m_scene->commit(tr("Group color"));
        m_applying = false;
        scheduleRebuild();
    };
    for (int i = 0; i < 8; ++i) {
        const QColor c = GroupItem::paletteColor(i);
        auto *b = new QToolButton;
        b->setFixedSize(20, 20);
        b->setStyleSheet(QStringLiteral("background:%1; border:%2;").arg(c.name(), c == g->color() ? "2px solid #000" : "1px solid #888"));
        connect(b, &QToolButton::clicked, this, [applyColor, c] { applyColor(c); });
        cl->addWidget(b);
    }
    auto *more = new QToolButton;
    more->setText(QStringLiteral("…"));
    connect(more, &QToolButton::clicked, this, [this, g, applyColor] {
        const QColor c = QColorDialog::getColor(g->color(), this, tr("Group color"));
        if (c.isValid())
            applyColor(c);
    });
    cl->addWidget(more);
    cl->addStretch(1);
    m_form->addRow(tr("Color"), colors);

    auto *style = new QComboBox;
    style->addItem(tr("Curved"), int(GroupItem::Curved));
    style->addItem(tr("Straight"), int(GroupItem::Straight));
    style->setCurrentIndex(g->style() == GroupItem::Straight ? 1 : 0);
    connect(style, &QComboBox::currentIndexChanged, this, [this, style] {
        m_applying = true;
        for (GroupItem *x : m_groups)
            x->setStyle(GroupItem::Style(style->currentData().toInt()));
        m_scene->commit(tr("Group line"));
        m_applying = false;
    });
    m_form->addRow(tr("Line"), style);

    if (single) {
        addSeparator(tr("Elements (%1)").arg(g->memberItems().size()));
        for (QGraphicsItem *m : g->memberItems()) {
            QString text;
            QString icon;
            if (m->type() == ItemType::Device) {
                const DeviceData &d = static_cast<DeviceItem *>(m)->data();
                text = d.id + QStringLiteral("  ") + d.displayName();
                icon = d.iconPath;
            } else if (m->type() == ItemType::Strip) {
                const DeviceData &d = static_cast<StripItem *>(m)->data();
                text = d.id + QStringLiteral("  ") + d.displayName();
                icon = d.iconPath;
            }
            auto *b = new QPushButton(IconCache::instance().icon(icon), text);
            b->setFlat(true);
            b->setStyleSheet(QStringLiteral("text-align:left;"));
            connect(b, &QPushButton::clicked, this, [this, m] {
                m_scene->clearSelection();
                m->setSelected(true);
            });
            m_form->addRow(b);
        }
        // links two by two, each one can be removed
        auto idOf = [this](const QString &uid) {
            QGraphicsItem *it = m_scene->itemByUid(uid);
            if (it && it->type() == ItemType::Device)
                return static_cast<DeviceItem *>(it)->data().id;
            if (it && it->type() == ItemType::Strip)
                return static_cast<StripItem *>(it)->data().id;
            return QStringLiteral("?");
        };
        addSeparator(tr("Links (%1)").arg(g->links().size()));
        for (int i = 0; i < g->links().size(); ++i) {
            const GroupItem::Link l = g->links().at(i);
            auto *row = new QWidget;
            auto *h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            h->addWidget(new QLabel(idOf(l.first) + QStringLiteral("  —  ") + idOf(l.second)), 1);
            auto *x = new QToolButton;
            x->setText(QStringLiteral("✕"));
            x->setToolTip(tr("Delete this link"));
            connect(x, &QToolButton::clicked, this, [this, g, i] {
                g->removeLink(i);
                if (g->links().isEmpty())
                    delete g;
                m_scene->commit(tr("Delete link"));
                scheduleRebuild();
            });
            h->addWidget(x);
            m_form->addRow(row);
        }
        auto *hint = new QLabel(tr("<i>Group tool (J): click an element, then the next one to add links.</i>"));
        hint->setWordWrap(true);
        m_form->addRow(hint);
    }
    auto *del = new QPushButton(tr("Delete group (keeps the elements)"));
    connect(del, &QPushButton::clicked, this, [this] {
        m_scene->deleteSelection();
    });
    m_form->addRow(del);
}

// ---------------------------------------------------------------- electrical panel

void PropertiesPanel::buildEquipment()
{
    EquipmentItem *first = m_equipment.first();
    const DeviceData &d = first->data();
    const bool single = m_equipment.size() == 1;
    addHeader(single ? d.displayName() : tr("%n elements selected", nullptr, m_equipment.size()),
              single ? tr("%1 × %2 mm, %n terminal(s)", nullptr, first->portCount())
                           .arg(first->size().width()).arg(first->size().height())
                     : tr("Changes apply to all selected"),
              single ? d.iconPath : QString());
    buildCommon(d, single);
    if (!single)
        return;

    // connections of every terminal: where each wire comes from / goes to
    const QList<WireItem *> ws = m_scene->wiresOf(d.uid);
    addSeparator(tr("Connections (%1)").arg(ws.size()));
    if (ws.isEmpty())
        m_form->addRow(new QLabel(tr("<i>No wires yet.</i> Wire tool (E): click a terminal, then the other terminal.")));
    for (int port = 0; port < first->portCount(); ++port) {
        for (WireItem *w : ws) {
            const bool fromHere = w->fromUid() == d.uid && w->fromPort() == port;
            const bool toHere = w->toUid() == d.uid && w->toPort() == port;
            if (!fromHere && !toHere)
                continue;
            EquipmentItem *other = fromHere ? w->toItem() : w->fromItem();
            const QString otherText = w->endText(!fromHere);
            auto *b = new QPushButton(QStringLiteral("%1  →  %2   (%3 mm²)")
                                          .arg(first->portName(port), otherText, w->crossSection()));
            b->setFlat(true);
            b->setStyleSheet(QStringLiteral("text-align:left; border-left: 6px solid %1; padding-left: 6px;").arg(w->color().name()));
            b->setToolTip(tr("Select %1").arg(other ? other->data().displayName() : QString()));
            connect(b, &QPushButton::clicked, this, [this, other] {
                if (!other)
                    return;
                m_scene->clearSelection();
                other->setSelected(true);
            });
            m_form->addRow(b);
        }
    }
}

void PropertiesPanel::buildWires()
{
    WireItem *w = m_wires.first();
    const bool single = m_wires.size() == 1;
    addHeader(single ? tr("Wire") : tr("%n wires", nullptr, m_wires.size()),
              single ? w->endText(true) + QStringLiteral("  →  ") + w->endText(false) : tr("Changes apply to all selected"));

    auto *color = new QComboBox;
    int current = -1;
    const auto colors = WireItem::standardColors();
    for (int i = 0; i < colors.size(); ++i) {
        QPixmap pm(16, 16);
        pm.fill(colors[i].second);
        color->addItem(QIcon(pm), colors[i].first, colors[i].second);
        if (colors[i].second == w->color())
            current = i;
    }
    if (current < 0) {
        QPixmap pm(16, 16);
        pm.fill(w->color());
        color->addItem(QIcon(pm), w->color().name().toUpper(), w->color());
        current = color->count() - 1;
    }
    color->addItem(tr("Other color..."));
    color->setCurrentIndex(current);
    connect(color, &QComboBox::activated, this, [this, color, w](int i) {
        QColor c = color->itemData(i).value<QColor>();
        if (!c.isValid()) {
            c = QColorDialog::getColor(w->color(), this, tr("Wire color"));
            if (!c.isValid()) {
                scheduleRebuild();
                return;
            }
        }
        m_applying = true;
        for (WireItem *x : m_wires)
            x->setColor(c);
        m_scene->setNewWireColor(c);
        m_scene->updateLinks();
        m_scene->commit(tr("Wire color"));
        m_applying = false;
        scheduleRebuild();
    });
    m_form->addRow(tr("Color"), color);

    auto *section = new QComboBox;
    section->setEditable(true);
    for (const char *v : {"0.5", "0.75", "1", "1.5", "2.5", "4", "6", "10", "16", "25"})
        section->addItem(QString::fromLatin1(v));
    section->setCurrentText(w->crossSection());
    auto applySection = [this, section] {
        const QString v = section->currentText().trimmed();
        m_applying = true;
        for (WireItem *x : m_wires)
            x->setCrossSection(v);
        m_scene->setNewWireSection(v);
        m_scene->commit(tr("Wire cross-section"));
        m_applying = false;
    };
    connect(section, &QComboBox::activated, this, applySection);
    connect(section->lineEdit(), &QLineEdit::editingFinished, this, applySection);
    m_form->addRow(tr("Cross-section (mm²)"), section);

    if (single) {
        auto *label = new QLineEdit(w->label());
        label->setPlaceholderText(tr("e.g. Lights living, F1"));
        connect(label, &QLineEdit::editingFinished, this, [this, label] {
            m_applying = true;
            m_wires.first()->setLabel(label->text().trimmed());
            m_scene->commit(tr("Wire label"));
            m_applying = false;
        });
        m_form->addRow(tr("Label"), label);

        auto *route = new QLabel(w->isManual() ? tr("Manual – drag the square grips (corners) or the round ones (segments); "
                                                     "double-click the wire to add a step")
                                               : tr("Automatic – drag a grip to shape it by hand"));
        route->setWordWrap(true);
        m_form->addRow(tr("Route"), route);
        if (w->isManual()) {
            auto *reset = new QPushButton(tr("Automatic route"));
            connect(reset, &QPushButton::clicked, this, [this] {
                m_applying = true;
                for (WireItem *x : m_wires)
                    x->resetRoute();
                m_scene->commit(tr("Automatic route"));
                m_applying = false;
                scheduleRebuild();
            });
            m_form->addRow(QString(), reset);
        }

        for (bool from : {true, false}) {
            EquipmentItem *e = from ? w->fromItem() : w->toItem();
            auto *b = new QPushButton(e ? IconCache::instance().icon(e->data().iconPath) : QIcon(),
                                      (from ? tr("From: ") : tr("To: ")) + w->endText(from));
            b->setFlat(true);
            b->setStyleSheet(QStringLiteral("text-align:left;"));
            connect(b, &QPushButton::clicked, this, [this, e] {
                if (!e)
                    return;
                m_scene->clearSelection();
                e->setSelected(true);
            });
            m_form->addRow(b);
        }
    }
}

void PropertiesPanel::buildRails()
{
    RailItem *r = m_rails.first();
    addHeader(m_rails.size() == 1 ? tr("DIN rail") : tr("%n DIN rails", nullptr, m_rails.size()),
              tr("TS35 – drag the right end to change the length"));
    auto *len = spin(20, 3000, r->length(), tr(" mm"), 0, 9);
    connect(len, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        m_applying = true;
        for (RailItem *x : m_rails)
            x->setLength(v);
        m_scene->commit(tr("Rail length"));
        m_applying = false;
    });
    m_form->addRow(tr("Length"), len);
    m_form->addRow(tr("Modules"), new QLabel(QString::number(int(r->length() / 18))));
    if (m_rails.size() == 1) {
        auto *label = new QLineEdit(r->label());
        label->setPlaceholderText(tr("e.g. Row 1 – lighting"));
        connect(label, &QLineEdit::editingFinished, this, [this, label] {
            m_applying = true;
            m_rails.first()->setLabel(label->text().trimmed());
            m_scene->commit(tr("Rail label"));
            m_applying = false;
        });
        m_form->addRow(tr("Label"), label);
    }
}
