/*
 * mfcg-model/08-scenes-start.js
 * Part 8/8: Scenes Start
 * Contains: TownScene, ToolForm (beginning ScaleBarOld scenes module)
 */
g["com.watabou.mfcg.scenes.TownScene"] = TownScene;
TownScene.__name__ = "com.watabou.mfcg.scenes.TownScene";
TownScene.getMenu = function() {
    null == TownScene.menu && (TownScene.menu = new Menu);
    return TownScene.menu
};
TownScene.applyPalette = function(a) {
    K.setPalette(a, !0);
    District.updateColors(City.instance.districts);
    Game.switchScene(ViewScene)
};
TownScene.editColors = function() {
    if (null == u.findWidnow(PaletteForm)) {
        var a = new PaletteForm(TownScene.applyPalette, "Default;default;Ink;ink;Black & White;bw;Vivid;vivid;Natural;natural;Modern;modern".split(";"));
        a.getName = PaletteForm.swatches(null, ["colorPaper", "colorRoof", "colorDark"]);
        K.fillForm(a);
        u.showDialog(a, "Color scheme")
    }
};
TownScene.__super__ = Scene;
TownScene.prototype = v(Scene.prototype, {
    get_mapScale: function() {
        var a = this.model.getViewport(),
            b = 40 * (null != this.model.focus ? .25 : 1),
            c = this.rWidth / (a.width + b);
        b = this.rHeight / (a.height + b);
        if (null != this.model.focus) return Math.min(c, b);
        a = Math.min(c, b);
        c = Math.max(c, b);
        return 2 < c / a ? c / 2 : a
    },
    activate: function() {
        Scene.prototype.activate.call(this);
        this.stage.set_color(K.colorPaper);
        Main.preview ||
            this.stage.addEventListener("rightClick", l(this, this.onRightClick))
    },
    deactivate: function() {
        Scene.prototype.deactivate.call(this);
        TownScene.map = null;
        this.stage.removeEventListener("rightClick", l(this, this.onRightClick))
    },
    onRightClick: function(a) {
        a = TownScene.getMenu();
        a.addSeparator();
        this.onMapContext(a);
        a.addSeparator();
        this.onContext(a);
        this.showMenu()
    },
    onContext: function(a) {},
    onMapContext: function(a) {},
    onEsc: function() {
        u.hideMenu() || (null != this.model.focus ? this.zoomIn(null) : Scene.prototype.onEsc.call(this))
    },
    onKeyEvent: function(a,
        b) {},
    createOverlays: function() {
        this.overlays = [];
        this.overlays.push(this.grid = new GridOverlay(this));
        this.addChild(this.grid);
        this.overlays.push(this.pins = new PinsOverlay(this));
        this.addChild(this.pins);
        this.overlays.push(this.markers = new mi(this));
        this.addChild(this.markers);
        this.overlays.push(this.scBar = new ScaleBarOverlay(this));
        this.addChild(this.scBar);
        this.overlays.push(this.compass = new CompassOverlay(this));
        this.addChild(this.compass);
        this.overlays.push(this.legend = new LegendOverlay(this));
        this.addChild(this.legend);
        this.overlays.push(this.labels =
            new LabelsOverlay(this));
        this.addChild(this.labels)
    },
    updateOverlays: function() {
        for (var a = 0, b = this.overlays; a < b.length;) {
            var c = b[a];
            ++a;
            c.update(this.model)
        }
    },
    resetOverlays: function() {
        for (var a = 0, b = this.overlays; a < b.length;) {
            var c = b[a];
            ++a;
            this.removeChild(c)
        }
        this.createOverlays();
        this.updateOverlays();
        this.toggleOverlays();
        a = 0;
        for (b = this.overlays; a < b.length;) c = b[a], ++a, c.setSize(this.rWidth, this.rHeight)
    },
    layout: function() {
        this.layoutMap();
        for (var a = 0, b = this.overlays; a < b.length;) {
            var c = b[a];
            ++a;
            c.setSize(this.rWidth,
                this.rHeight)
        }
        this.legend.update(City.instance)
    },
    recreateMap: function() {
        null != TownScene.map && this.removeChild(TownScene.map);
        TownScene.map = new FormalMap(this.model);
        this.addChildAt(TownScene.map, 0)
    },
    layoutMap: function() {
        var a = this.model.getViewport(),
            b = this.get_mapScale(),
            c = 1 / b;
        if (null == TownScene.map || K.lineInvScale != c) K.lineInvScale = c, this.recreateMap();
        TownScene.map.set_scaleX(TownScene.map.set_scaleY(b));
        TownScene.map.set_x(this.rWidth / 2 - (a.get_left() + a.get_right()) / 2 * b);
        TownScene.map.set_y(this.rHeight / 2 - (a.get_top() + a.get_bottom()) / 2 * b);
        a = TownScene.map.globalToLocal(this.localToGlobal(new I(0,
            0)));
        b = TownScene.map.globalToLocal(this.localToGlobal(new I(this.rWidth, this.rHeight)));
        TownScene.map.updateBounds(a.x, b.x, a.y, b.y)
    },
    layoutLabels: function() {
        this.labels.setSize(this.rWidth, this.rHeight);
        this.toggleOverlays()
    },
    drawMap: function() {
        this.stage.set_color(K.colorPaper);
        this.recreateMap();
        this.layoutMap();
        this.layoutLabels()
    },
    showMenu: function(a) {
        null != a ? u.showMenuAt(TownScene.menu, a.get_x() + a.rWidth, a.get_y() + a.rHeight + 2) : u.showMenu(TownScene.menu);
        TownScene.menu = null
    },
    loadPreset: function(a) {
        TownScene.applyPalette(Palette.fromAsset(a))
    },
    toggleOverlays: function(a) {
        a = "Legend" == State.get("districts", "Curved") || "Legend" == State.get("landmarks");
        this.legend.set_visible(a);
        this.legend.get_visible() && this.legend.update(this.model);
        this.pins.set_visible("Legend" == State.get("districts", "Curved"));
        this.markers.set_visible("Hidden" != State.get("landmarks"));
        var b = State.get("districts", "Curved");
        this.labels.set_visible("Straight" == b || "Curved" == b);
        this.scBar.set_visible(State.get("scale_bar", !0) && !a);
        this.compass.set_visible(State.get("compass", !0));
        this.grid.set_visible(State.get("grid",
            !0))
    },
    arrangeOverlays: function() {
        this.legend.get_visible() && this.legend.get_position() == yc.BOTTOM_RIGHT ? this.compass.set_position(yc.BOTTOM_LEFT) : this.compass.set_position(yc.BOTTOM_RIGHT)
    },
    zoomIn: function(a) {
        a = null != a ? Focus.district(a) : null;
        this.model.focus = a;
        this.recreateMap();
        this.labels.update(this.model);
        this.layout()
    },
    __class__: TownScene,
    __properties__: v(Scene.prototype.__properties__, {
        get_mapScale: "get_mapScale"
    })
});
var ToolForm = function() {
    Form.call(this)
};
g["com.watabou.mfcg.RotateTool.forms.ToolForm"] = ToolForm;
ToolForm.__name__ =
    "com.watabou.mfcg.RotateTool.forms.ToolForm";
ToolForm.loadSaved = function(a) {
    ToolForm.saved = State.get("tools");
    null == ToolForm.saved && (ToolForm.saved = {});
    if (null != a)
        for (var b = 0; b < a.length;) {
            var c = a[b];
            ++b;
            var d = ToolForm.saved[c.__name__];
            null != d && d.visible && null == u.findWidnow(c) && (c = w.createInstance(c, []), u.showDialog(c), c.restore())
        }
};
ToolForm.__super__ = Form;
ToolForm.prototype = v(Form.prototype, {
    onShow: function() {
        var a = this;
        this.dialog.onMove.add(l(this, this.onMove));
        this.dialog.onMinimize.add(function(b) {
            a.save(!0)
        });
        this.dialog.minimizable = !0
    },
    onHide: function() {
        this.save(!1)
    },
    onMove: function(a) {
        var b = a.getAdjustment();
        null != b && (a.set_x(b.x), a.set_y(b.y));
        this.save(!0)
    },
    onKey: function(a) {
        return !1
    },
    id: function() {
        return va.getClass(this).__name__
    },
    save: function(a) {
        var b = this.dialog;
        b = new I(b.get_x() + b.rWidth / 2, b.get_y() + b.rHeight / 2);
        ToolForm.saved[this.id()] = {
            x: b.x / u.layer.get_width(),
            y: b.y / u.layer.get_height(),
            visible: a,
            minimized: this.dialog.minimized
        };
        State.set("tools", ToolForm.saved)
    },
    restore: function() {
        var a = this.id();
        a = ToolForm.saved[a];
        null != a && (this.dialog.set_x(a.x * u.layer.get_width() -
            this.dialog.get_width() / 2 | 0), this.dialog.set_y(a.y * u.layer.get_height() - this.dialog.get_height() / 2 | 0), this.dialog.setMinimized(a.minimized), a = this.dialog.getAdjustment(), null != a && (this.dialog.set_x(a.x), this.dialog.set_y(a.y)));
        this.save(!0)
    },
    forceDisplay: function() {
        ToolForm.saved[this.id()].visible = !0
    },
    __class__: ToolForm
});
var Kd = function() {
    var a = this;
    Form.call(this);
    var b = this.createFeaturesTab(),
        c = this.createRoadsTab();
    this.tabs = new Tabs;
    this.tabs.addTab("Features", b);
    this.tabs.addTab("Roads", c);
    this.buttons =
        new VBox;
    this.buttons.setMargins(10, 8);
    b = new Button("Small");
    b.set_width(80);
    b.click.add(function() {
        a.build(Math.floor(10 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * 10))
    });
    this.buttons.add(b);
    b = new Button("Medium");
    b.set_width(80);
    b.click.add(function() {
