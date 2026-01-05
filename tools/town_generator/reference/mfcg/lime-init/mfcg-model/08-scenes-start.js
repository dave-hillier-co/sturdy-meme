/*
 * mfcg-model/08-scenes-start.js
 * Part 8/8: Scenes Start
 * Contains: TownScene, ToolForm (beginning of scenes module)
 */
g["com.watabou.mfcg.scenes.TownScene"] = ia;
ia.__name__ = "com.watabou.mfcg.scenes.TownScene";
ia.getMenu = function() {
    null == ia.menu && (ia.menu = new dd);
    return ia.menu
};
ia.applyPalette = function(a) {
    K.setPalette(a, !0);
    Pe.updateColors(Ub.instance.districts);
    bb.switchScene(Ec)
};
ia.editColors = function() {
    if (null == u.findWidnow(Hc)) {
        var a = new Hc(ia.applyPalette, "Default;default;Ink;ink;Black & White;bw;Vivid;vivid;Natural;natural;Modern;modern".split(";"));
        a.getName = Hc.swatches(null, ["colorPaper", "colorRoof", "colorDark"]);
        K.fillForm(a);
        u.showDialog(a, "Color scheme")
    }
};
ia.__super__ = Pb;
ia.prototype = v(Pb.prototype, {
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
        Pb.prototype.activate.call(this);
        this.stage.set_color(K.colorPaper);
        sb.preview ||
            this.stage.addEventListener("rightClick", l(this, this.onRightClick))
    },
    deactivate: function() {
        Pb.prototype.deactivate.call(this);
        ia.map = null;
        this.stage.removeEventListener("rightClick", l(this, this.onRightClick))
    },
    onRightClick: function(a) {
        a = ia.getMenu();
        a.addSeparator();
        this.onMapContext(a);
        a.addSeparator();
        this.onContext(a);
        this.showMenu()
    },
    onContext: function(a) {},
    onMapContext: function(a) {},
    onEsc: function() {
        u.hideMenu() || (null != this.model.focus ? this.zoomIn(null) : Pb.prototype.onEsc.call(this))
    },
    onKeyEvent: function(a,
        b) {},
    createOverlays: function() {
        this.overlays = [];
        this.overlays.push(this.grid = new ki(this));
        this.addChild(this.grid);
        this.overlays.push(this.pins = new li(this));
        this.addChild(this.pins);
        this.overlays.push(this.markers = new mi(this));
        this.addChild(this.markers);
        this.overlays.push(this.scBar = new ni(this));
        this.addChild(this.scBar);
        this.overlays.push(this.compass = new qg(this));
        this.addChild(this.compass);
        this.overlays.push(this.legend = new Uc(this));
        this.addChild(this.legend);
        this.overlays.push(this.labels =
            new oi(this));
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
        this.legend.update(Ub.instance)
    },
    recreateMap: function() {
        null != ia.map && this.removeChild(ia.map);
        ia.map = new bi(this.model);
        this.addChildAt(ia.map, 0)
    },
    layoutMap: function() {
        var a = this.model.getViewport(),
            b = this.get_mapScale(),
            c = 1 / b;
        if (null == ia.map || K.lineInvScale != c) K.lineInvScale = c, this.recreateMap();
        ia.map.set_scaleX(ia.map.set_scaleY(b));
        ia.map.set_x(this.rWidth / 2 - (a.get_left() + a.get_right()) / 2 * b);
        ia.map.set_y(this.rHeight / 2 - (a.get_top() + a.get_bottom()) / 2 * b);
        a = ia.map.globalToLocal(this.localToGlobal(new I(0,
            0)));
        b = ia.map.globalToLocal(this.localToGlobal(new I(this.rWidth, this.rHeight)));
        ia.map.updateBounds(a.x, b.x, a.y, b.y)
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
        null != a ? u.showMenuAt(ia.menu, a.get_x() + a.rWidth, a.get_y() + a.rHeight + 2) : u.showMenu(ia.menu);
        ia.menu = null
    },
    loadPreset: function(a) {
        ia.applyPalette(Xc.fromAsset(a))
    },
    toggleOverlays: function(a) {
        a = "Legend" == ba.get("districts", "Curved") || "Legend" == ba.get("landmarks");
        this.legend.set_visible(a);
        this.legend.get_visible() && this.legend.update(this.model);
        this.pins.set_visible("Legend" == ba.get("districts", "Curved"));
        this.markers.set_visible("Hidden" != ba.get("landmarks"));
        var b = ba.get("districts", "Curved");
        this.labels.set_visible("Straight" == b || "Curved" == b);
        this.scBar.set_visible(ba.get("scale_bar", !0) && !a);
        this.compass.set_visible(ba.get("compass", !0));
        this.grid.set_visible(ba.get("grid",
            !0))
    },
    arrangeOverlays: function() {
        this.legend.get_visible() && this.legend.get_position() == yc.BOTTOM_RIGHT ? this.compass.set_position(yc.BOTTOM_LEFT) : this.compass.set_position(yc.BOTTOM_RIGHT)
    },
    zoomIn: function(a) {
        a = null != a ? fh.district(a) : null;
        this.model.focus = a;
        this.recreateMap();
        this.labels.update(this.model);
        this.layout()
    },
    __class__: ia,
    __properties__: v(Pb.prototype.__properties__, {
        get_mapScale: "get_mapScale"
    })
});
var vc = function() {
    oc.call(this)
};
g["com.watabou.mfcg.ui.forms.ToolForm"] = vc;
vc.__name__ =
    "com.watabou.mfcg.ui.forms.ToolForm";
vc.loadSaved = function(a) {
    vc.saved = ba.get("tools");
    null == vc.saved && (vc.saved = {});
    if (null != a)
        for (var b = 0; b < a.length;) {
            var c = a[b];
            ++b;
            var d = vc.saved[c.__name__];
            null != d && d.visible && null == u.findWidnow(c) && (c = w.createInstance(c, []), u.showDialog(c), c.restore())
        }
};
vc.__super__ = oc;
vc.prototype = v(oc.prototype, {
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
        vc.saved[this.id()] = {
            x: b.x / u.layer.get_width(),
            y: b.y / u.layer.get_height(),
            visible: a,
            minimized: this.dialog.minimized
        };
        ba.set("tools", vc.saved)
    },
    restore: function() {
        var a = this.id();
        a = vc.saved[a];
        null != a && (this.dialog.set_x(a.x * u.layer.get_width() -
            this.dialog.get_width() / 2 | 0), this.dialog.set_y(a.y * u.layer.get_height() - this.dialog.get_height() / 2 | 0), this.dialog.setMinimized(a.minimized), a = this.dialog.getAdjustment(), null != a && (this.dialog.set_x(a.x), this.dialog.set_y(a.y)));
        this.save(!0)
    },
    forceDisplay: function() {
        vc.saved[this.id()].visible = !0
    },
    __class__: vc
});
var Kd = function() {
    var a = this;
    oc.call(this);
    var b = this.createFeaturesTab(),
        c = this.createRoadsTab();
    this.tabs = new ig;
    this.tabs.addTab("Features", b);
    this.tabs.addTab("Roads", c);
    this.buttons =
        new gb;
    this.buttons.setMargins(10, 8);
    b = new fb("Small");
    b.set_width(80);
    b.click.add(function() {
        a.build(Math.floor(10 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * 10))
    });
    this.buttons.add(b);
    b = new fb("Medium");
    b.set_width(80);
    b.click.add(function() {
