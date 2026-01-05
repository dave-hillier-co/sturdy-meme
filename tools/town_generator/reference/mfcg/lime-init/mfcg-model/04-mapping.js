/*
 * mfcg-model/04-mapping.js
 * Part 4/8: Mapping Views and Painters
 * Contains: BuildingPainter, FarmPainter, RiverView, RoadsView, Style, etc.
 */
            g["com.watabou.mfcg.mapping.BuildingPainter"] = hd;
            hd.__name__ = "com.watabou.mfcg.mapping.BuildingPainter";
            hd.paint = function(a, b, c, d, f, h) {
                null == h && (h = !1);
                null == f && (f = .5);
                if (h && kc.drawSolid) hd.drawSolids(a, b);
                else {
                    kc.raised || (f = 0);
                    var k = K.getStrokeWidth(K.strokeNormal, !0),
                        n = 0;
                    if (0 < f) {
                        n = -1.2 * f;
                        var p = K.colorWall,
                            g = function(b) {
                                for (var c = [], d = 0; d < b.length;) {
                                    var f = b[d];
                                    ++d;
                                    c.push(new I(f.x, f.y + n))
                                }
                                c.reverse();
                                a.beginFill(p);
                                a.lineStyle(k, K.colorDark);
                                Kb.drawPolygon(a, b.concat(c));
                                a.endFill();
                                if (K.colorDark != p) {
                                    a.lineStyle(k, K.colorDark);
                                    c = 1;
                                    for (d = b.length - 1; c < d;) f = c++, f = b[f], a.moveTo(f.x, f.y), a.lineTo(f.x, f.y + n);
                                    a.endFill()
                                }
                            };
                        for (f = 0; f < b.length;) {
                            h = b[f];
                            ++f;
                            for (var q = null, m = h.length, u = 0, r = m; u < r;) {
                                var l = u++,
                                    x = h[l];
                                l = h[(l + 1) % m];
                                l.x < x.x ? null == q ? q = [x, l] : q.push(l) : null != q && (g(q), q = null)
                            }
                            null != q && g(q)
                        }
                    }
                    g = 1 < b.length && "Block" != kc.planMode && ba.get("weathered_roofs", !1) ? K.weathering / 100 : 0;
                    for (f = 0; f < b.length;) h = b[f], ++f, q = 0 == g ? c : Gc.scale(c, Math.pow(2, (((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3 * 2 - 1) * g)), m = hd.outlineNormal ? d : q, a.beginFill(q), 0 != n ? (a.lineStyle(k, m), Kb.drawPolygonAt(a, h, 0, n)) : (a.lineStyle(k, m, null, null,
                        null, null, 1), Kb.drawPolygon(a, h)), a.endFill(), hd.outlineNormal && "Block" != kc.planMode && hd.drawRoof(a, h, n, k, m)
                }
            };
            hd.drawSolids = function(a, b) {
                a.beginFill(K.colorWall);
                hd.outlineSolid && K.colorDark != K.colorWall && a.lineStyle(K.getStrokeWidth(K.strokeNormal, !0), K.colorDark, null, null, null, null, 1);
                for (var c = 0; c < b.length;) {
                    var d = b[c];
                    ++c;
                    Kb.drawPolygon(a, d)
                }
                a.endFill()
            };
            hd.drawRoof = function(a, b, c, d, f) {
                if ("Plain" != kc.roofStyle && !sb.preview) {
                    for (var h = [], k = 0; k < b.length;) {
                        var n = b[k];
                        ++k;
                        h.push(n.add(I.polar(.1 *
                            (((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3 * 2 - 1), Math.PI * ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647))))
                    }
                    k = new jf(h, !0);
                    if ("Gable" == kc.roofStyle) {
                        if (0 < k.ribs.length) return;
                        k.addGables()
                    }
                    a.lineStyle(d, f);
                    h = 0;
                    for (k = k.bones; h < k.length;) d = k[h], ++h, 1.2 < d.slope.get_length() && (f = d.a.point, b = d.b.point, 0 != c && (f = new I(f.x, f.y + (c - d.a.height / 3)), b = new I(b.x, b.y + (c - d.b.height / 3))), a.moveTo(f.x, f.y), a.lineTo(b.x,
                        b.y));
                    a.endFill()
                }
            };
            var Yk = function() {};
            g["com.watabou.mfcg.mapping.FarmPainter"] = Yk;
            Yk.__name__ = "com.watabou.mfcg.mapping.FarmPainter";
            Yk.paint = function(a, b) {
                var c = ba.get("farm_fileds", "Furrows");
                if ("Hidden" != c && !sb.preview) {
                    a = a.get_graphics();
                    var d = ba.get("outline_fields", !1);
                    if ("Plain" == c) {
                        a.beginFill(K.colorGreen);
                        d && a.lineStyle(K.getStrokeWidth(K.strokeNormal, !0), K.colorDark);
                        c = 0;
                        for (var f = b.subPlots; c < f.length;) d = f[c], ++c, Kb.drawPolygon(a, d)
                    } else {
                        a.lineStyle(K.getStrokeWidth(K.strokeNormal,
                            !0), K.colorGreen, null, null, null, 0);
                        c = 0;
                        for (f = b.furrows; c < f.length;) {
                            var h = f[c];
                            ++c;
                            var k = h.start;
                            h = h.end;
                            a.moveTo(k.x, k.y);
                            a.lineTo(h.x, h.y)
                        }
                        if (d)
                            for (c = 0, f = b.subPlots; c < f.length;) d = f[c], ++c, Kb.drawPolygon(a, d)
                    }
                    a.endFill();
                    d = kc.planMode;
                    "Block" != d && "Hidden" != d && hd.paint(a, b.buildings, K.colorRoof, K.colorDark, .3)
                }
            };
            var fh = function(a) {
                this.faces = a;
                this.edges = [];
                this.vertices = [];
                for (var b = 0; b < a.length;) {
                    var c = a[b];
                    ++b;
                    for (var d = c = c.halfEdge, f = !0; f;) {
                        var h = d;
                        d = d.next;
                        f = d != c;
                        this.edges.push(h);
                        Z.add(this.vertices,
                            h.origin)
                    }
                }
            };
            g["com.watabou.mfcg.mapping.Focus"] = fh;
            fh.__name__ = "com.watabou.mfcg.mapping.Focus";
            fh.district = function(a) {
                return new fh(a.faces)
            };
            fh.prototype = {
                getBounds: function() {
                    null == this.bounds && this.updateBounds();
                    return this.bounds
                },
                updateBounds: function() {
                    for (var a = Infinity, b = Infinity, c = -Infinity, d = -Infinity, f = 0, h = this.vertices; f < h.length;) {
                        var k = h[f];
                        ++f;
                        k = k.point;
                        a > k.x && (a = k.x);
                        b > k.y && (b = k.y);
                        c < k.x && (c = k.x);
                        d < k.y && (d = k.y)
                    }
                    this.bounds = new na(a, b, c - a, d - b)
                },
                __class__: fh
            };
            var ai = function(a) {
                ka.call(this);
                if (null != a) {
                    var b = this.get_graphics(),
                        c = 5 * K.lineInvScale;
                    a = Ua.toPoly(a.faces[0].data.district.border);
                    b.lineStyle(c, D.black, .4, !1, null, 0, 1);
                    Kb.dashedPolyline(b, a, !0, [2 * c, 2 * c])
                }
            };
            g["com.watabou.mfcg.mapping.FocusView"] = ai;
            ai.__name__ = "com.watabou.mfcg.mapping.FocusView";
            ai.__super__ = ka;
            ai.prototype = v(ka.prototype, {
                __class__: ai
            });
            var bi = function(a) {
                ka.call(this);
                this.model = a;
                kc.planMode = sb.preview ? "Lots" : ba.get("display_mode", "Lots");
                kc.roofStyle = ba.get("roof_style", "Plain");
                kc.raised = ba.get("raised",
                    !0) && !sb.preview;
                kc.watercolours = ba.get("watercolours", !1);
                kc.drawSolid = ba.get("draw_solids", !0);
                hd.outlineNormal = ba.get("outline_buildings", !0) || sb.preview;
                hd.outlineSolid = ba.get("outline_solids", !0) || sb.preview;
                this.roads = new ie;
                this.roads.update(a);
                this.addChild(this.roads);
                for (var b = 0, c = a.canals; b < c.length;) {
                    var d = c[b];
                    ++b;
                    var f = new mg;
                    f.update(a, d);
                    this.addChild(f)
                }
                this.patches = [];
                if (null == a.focus) c = a.cells;
                else {
                    b = [];
                    c = 0;
                    for (d = a.focus.faces; c < d.length;) f = d[c], ++c, b.push(f.data);
                    f = b;
                    var h = [];
                    b = 0;
                    c = [];
                    d = 0;
                    for (var k = f; d < k.length;) {
                        var n = k[d];
                        ++d;
                        n.landing && c.push(n)
                    }
                    for (d = c; b < d.length;) {
                        n = d[b];
                        ++b;
                        c = 0;
                        k = [];
                        for (var p = n = n.face.halfEdge, g = !0; g;) {
                            var q = p;
                            p = p.next;
                            g = p != n;
                            null != q.twin && k.push(q.twin.face.data)
                        }
                        p = [];
                        for (g = 0; g < k.length;) n = k[g], ++g, n.waterbody && p.push(n);
                        for (k = p; c < k.length;) n = k[c], ++c, Z.add(h, n)
                    }
                    c = f.concat(h)
                }
                for (b = 0; b < c.length;) n = c[b], ++b, d = new kc(n), d.draw() && (this.patches.push(d), this.addChild(d));
                ba.get("show_trees", !1) && "Block" != kc.planMode && !sb.preview && this.addChild(new gc(a));
                b = new ng;
                this.addChild(b);
                null != a.wall && b.draw(a.wall, !1, a.focus);
                null != a.citadel && b.draw(va.__cast(a.citadel.ward, xd).wall, !0, a.focus);
                null != a.focus && this.addChild(new ai(a.focus));
                this.mouseEnabled = !1
            };
            g["com.watabou.mfcg.mapping.FormalMap"] = bi;
            bi.__name__ = "com.watabou.mfcg.mapping.FormalMap";
            bi.__super__ = ka;
            bi.prototype = v(ka.prototype, {
                updateBounds: function(a, b, c, d) {
                    0 < this.model.shoreE.length && (null != this.water && this.removeChild(this.water), this.water = new md, this.drawWaterbody(this.water.get_graphics(),
                        a, b, c, d), this.addChildAt(this.water, 0))
                },
                updateRoads: function() {
                    this.roads.update(this.model)
                },
                updateTrees: function() {
                    for (var a = 0, b = this.get_numChildren(); a < b;) {
                        var c = a++,
                            d = this.getChildAt(c);
                        if (d instanceof gc) {
                            this.removeChild(d);
                            this.addChildAt(new gc(this.model), c);
                            break
                        }
                    }
                },
                drawWaterbody: function(a, b, c, d, f) {
                    for (var h = this.model.getOcean(), k = (b + c) / 2, n = (d + f) / 2, p = [], g = 0; g < h.length;) {
                        var q = h[g];
                        ++g; - 1 != this.model.horizon.indexOf(q) ? p.push(new I(q.x < k ? b : c, q.y < n ? d : f)) : p.push(new I(Fc.gate(q.x, b, c),
                            Fc.gate(q.y, d, f)))
                    }
                    h = p;
                    ba.get("outline_water", !0) && a.lineStyle(K.getStrokeWidth(K.strokeNormal, !0), K.colorDark);
                    a.beginFill(K.colorWater);
                    Kb.drawPolygon(a, h);
                    a.endFill()
                },
                __class__: bi
            });
            var md = function() {
                S.call(this);
                this.__drawableType = 3
            };
            g["openfl.display.Shape"] = md;
            md.__name__ = "openfl.display.Shape";
            md.__super__ = S;
            md.prototype = v(S.prototype, {
                get_graphics: function() {
                    null == this.__graphics && (this.__graphics = new Ed(this));
                    return this.__graphics
                },
                __class__: md,
                __properties__: v(S.prototype.__properties__, {
                    get_graphics: "get_graphics"
                })
            });
            var kc = function(a) {
                md.call(this);
                this.patch = a;
                a.view = this;
                this.g = this.get_graphics()
            };
            g["com.watabou.mfcg.mapping.PatchView"] = kc;
            kc.__name__ = "com.watabou.mfcg.mapping.PatchView";
            kc.__super__ = md;
            kc.prototype = v(md.prototype, {
                draw: function() {
                    this.g.clear();
                    var a = va.getClass(this.patch.ward);
                    switch (a) {
                        case Pc:
                        case xd:
                        case Ne:
                        case he:
                            var b = K.colorRoof,
                                c = K.colorDark;
                            if (kc.watercolours) {
                                var d = this.patch.ward.getColor();
                                b = d;
                                c = Gc.lerp(c, d, .3)
                            }
                            switch (a) {
                                case Pc:
                                    var f = va.__cast(this.patch.ward,
                                        Pc).group;
                                    if (f.core == this.patch.face)
                                        for (a = 0, d = f.blocks; a < d.length;) {
                                            var h = d[a];
                                            ++a;
                                            var k = h == f.church;
                                            switch (kc.planMode) {
                                                case "Block":
                                                    h = [h.shape];
                                                    break;
                                                case "Complex":
                                                    null == h.buildings && h.createBuildings();
                                                    h = h.buildings;
                                                    break;
                                                case "Lots":
                                                    h = h.lots;
                                                    break;
                                                case "Simple":
                                                    null == h.rects && h.createRects();
                                                    h = h.rects;
                                                    break;
                                                default:
                                                    h = []
                                            }
                                            hd.paint(this.g, h, b, c, null, k)
                                        }
                                    break;
                                case xd:
                                    K.colorRoad != K.colorPaper && (this.g.beginFill(K.colorRoad), Kb.drawPolygon(this.g, this.patch.shape));
                                    a = va.__cast(this.patch.ward, xd).building;
                                    hd.paint(this.g, [a], b, c, 1, !0);
                                    break;
                                case Ne:
                                    a = va.__cast(this.patch.ward, Ne).building;
                                    hd.paint(this.g, a, b, c, 1, !0);
                                    break;
                                case he:
                                    a = va.__cast(this.patch.ward, he), "Block" != kc.planMode && hd.paint(this.g, [a.monument], b, c, 0, !0)
                            }
                            break;
                        case yd:
                            Yk.paint(this, this.patch.ward);
                            break;
                        case lf:
                            b = ba.get("outline_water", !0);
                            a = 0;
                            for (d = va.__cast(this.patch.ward, lf).piers; a < d.length;) c = d[a], ++a, this.drawPier(c, b);
                            break;
                        case ce:
                            this.drawGreen(va.__cast(this.patch.ward, ce).green);
                            break;
                        default:
                            return !1
                    }
                    return !0
                },
                drawGreen: function(a) {
                    this.g.beginFill(K.colorGreen);
                    Kb.drawPolygon(this.g, a)
                },
                drawPier: function(a, b) {
                    var c = a[0],
                        d = a[1];
                    if (b)
                        if (b = K.getStrokeWidth(K.strokeNormal, !0), 1.2 < b) {
                            this.g.lineStyle(b, K.colorDark, null, null, null, 0);
                            var f = this.g;
                            f.moveTo(c.x, c.y);
                            f.lineTo(d.x, d.y)
                        } else {
                            c = c.subtract(d);
                            c.normalize(b);
                            this.g.lineStyle(1.2 + b, K.colorDark, null, null, null, 0);
                            f = this.g;
                            d = a[0];
                            var h = a[1];
                            f.moveTo(d.x, d.y);
                            f.lineTo(h.x, h.y);
                            this.g.lineStyle(1.2 - b, K.colorPaper, null, null, null, 0);
                            f = this.g;
                            d = a[0].add(c);
                            h = a[1].add(c);
                            f.moveTo(d.x, d.y);
                            f.lineTo(h.x, h.y)
                        }
                    else this.g.lineStyle(1.2,
                        K.colorPaper, null, null, null, 0), f = this.g, f.moveTo(c.x, c.y), f.lineTo(d.x, d.y)
                },
                __class__: kc
            });
            var mg = function() {
                ka.call(this)
            };
            g["com.watabou.mfcg.mapping.RiverView"] = mg;
            mg.__name__ = "com.watabou.mfcg.mapping.RiverView";
            mg.getSegments = function(a, b) {
                for (var c = [], d = null, f = 0, h = a.length; f < h;) {
                    var k = f++,
                        n = a[k]; - 1 != b.edges.indexOf(n) || -1 != b.edges.indexOf(n.twin) ? null == d ? (d = [n], c.push(d), 0 < k && d.splice(0, 0, a[k - 1])) : d.push(n) : (null != d && d.push(n), d = null)
                }
                return c
            };
            mg.__super__ = ka;
            mg.prototype = v(ka.prototype, {
                update: function(a,
                    b) {
                    var c = 0 < a.shoreE.length,
                        d = [],
                        f = b.bridges,
                        h = f;
                    for (f = f.keys(); f.hasNext();) {
                        var k = f.next();
                        h.get(k);
                        var n = k;
                        d.push(n.point)
                    }
                    h = d;
                    if (null != a.focus)
                        for (f = mg.getSegments(b.course, a.focus), d = 0; d < f.length;) k = f[d], ++d, k = Ua.toPolyline(k), k = Hf.render(k, !1, 3, h), this.drawCourse(k, b.width);
                    else k = Ua.toPolyline(b.course), c && (k[0] = qa.lerp(k[0], k[1])), k = Hf.render(k, !1, 3, h), this.drawCourse(k, b.width);
                    !c || null != a.focus && -1 == a.focus.vertices.indexOf(b.course[0].origin) || this.drawMouth(b);
                    c = (b.width + 1.2) / 2;
                    h = f = b.bridges;
                    for (f = f.keys(); f.hasNext();)
                        if (k = f.next(), d = h.get(k), n = k, k = d, null == a.focus || -1 != a.focus.vertices.indexOf(n)) {
                            d = n.point;
                            var p = Ua.indexByOrigin(b.course, n),
                                g = b.course[p];
                            g = g.next.origin.point.subtract(g.origin.point);
                            0 < p && (p = b.course[p - 1], p = p.next.origin.point.subtract(p.origin.point), g.x += p.x, g.y += p.y);
                            if (null == k) k = new I(-g.y, g.x), n = c, null == n && (n = 1), k = k.clone(), k.normalize(n), this.drawBridge(d.subtract(k), d.add(k), 1.2);
                            else {
                                n = Ua.indexByOrigin(k, n);
                                if (0 == n) {
                                    g = new I(-g.y, g.x);
                                    p = c;
                                    null == p && (p = 1);
                                    g = g.clone();
                                    g.normalize(p);
                                    p = d.add(g);
                                    var q = d.subtract(g);
                                    k = k[n];
                                    k = k.next.origin.point.subtract(k.origin.point);
                                    n = c;
                                    null == n && (n = 1);
                                    k = k.clone();
                                    k.normalize(n);
                                    g = d.add(k);
                                    d = I.distance(g, p) > I.distance(g, q) ? p : q
                                } else p = k[n - 1], p = p.next.origin.point.subtract(p.origin.point), q = k[n], p = p.add(q.next.origin.point.subtract(q.origin.point)), g = Math.abs((p.x * g.y - p.y * g.x) / p.get_length() / g.get_length()), p = c / g, g = k[n], g = g.next.origin.point.subtract(g.origin.point), q = p, null == q && (q = 1), g = g.clone(), g.normalize(q), g = d.add(g), k = k[n - 1],
                                    k = k.next.origin.point.subtract(k.origin.point), n = p, null == n && (n = 1), k = k.clone(), k.normalize(n), d = d.subtract(k);
                                this.drawBridge(g, d, 2)
                            }
                        }
                },
                drawCourse: function(a, b) {
                    var c = this.get_graphics();
                    ba.get("outline_water", !0) ? (c.lineStyle(b, K.colorDark, null, !1, null, 0), Kb.drawPolyline(c, a), c.lineStyle(b - 2 * K.getStrokeWidth(K.strokeNormal, !0), K.colorWater)) : c.lineStyle(b, K.colorWater);
                    Kb.drawPolyline(c, a);
                    c.endFill()
                },
                drawBridge: function(a, b, c) {
                    var d = this.get_graphics();
                    if (ba.get("outline_roads", !0) || ba.get("outline_water",
                            !0)) {
                        var f = K.getStrokeWidth(K.strokeNormal, !0);
                        d.lineStyle(c + f, K.colorDark, null, !1, null, 0);
                        d.moveTo(a.x, a.y);
                        d.lineTo(b.x, b.y);
                        d.lineStyle(c - f, K.colorRoad)
                    } else d.lineStyle(c, K.colorRoad);
                    d.moveTo(a.x, a.y);
                    d.lineTo(b.x, b.y)
                },
                drawMouth: function(a) {
                    var b = K.getStrokeWidth(K.strokeNormal, !0),
                        c = a.width - b,
                        d = a.course[0].origin.point,
                        f = a.city.shore,
                        h = f.indexOf(d),
                        k = a.course[0],
                        n = k.next.origin.point.subtract(k.origin.point),
                        p = new I(.5 * n.x, .5 * n.y);
                    k = a.course[0];
                    var g = qa.lerp(k.origin.point, k.next.origin.point,
                        .5);
                    n = new I(-p.y, p.x);
                    var q = c / 2;
                    null == q && (q = 1);
                    n = n.clone();
                    n.normalize(q);
                    a = g.add(n);
                    k = qa.lerp(f[(h + f.length - 1) % f.length], d);
                    var m = a.subtract(p),
                        u = qa.lerp(k, d);
                    n = new I(-p.y, p.x);
                    q = c / 2;
                    null == q && (q = 1);
                    n = n.clone();
                    n.normalize(q);
                    c = g.subtract(n);
                    p = c.subtract(p);
                    n = qa.lerp(f[(h + 1) % f.length], d);
                    g = qa.lerp(n, d);
                    q = this.get_graphics();
                    q.beginFill(K.colorWater);
                    q.moveTo(a.x, a.y);
                    q.cubicCurveTo(m.x, m.y, u.x, u.y, k.x, k.y);
                    kf.isConvexVertexi(f, h) && q.lineTo(d.x, d.y);
                    q.lineTo(n.x, n.y);
                    q.cubicCurveTo(g.x, g.y, p.x, p.y,
                        c.x, c.y);
                    q.endFill();
                    ba.get("outline_water", !0) && (q.lineStyle(b, K.colorDark), q.moveTo(a.x, a.y), q.cubicCurveTo(m.x, m.y, u.x, u.y, k.x, k.y), q.moveTo(c.x, c.y), q.cubicCurveTo(p.x, p.y, g.x, g.y, n.x, n.y), q.moveTo(0, 0), q.endFill())
                },
                __class__: mg
            });
            var ie = function() {
                ka.call(this);
                this.outline = new ka;
                this.addChild(this.outline);
                this.fill = new ka;
                this.addChild(this.fill)
            };
            g["com.watabou.mfcg.mapping.RoadsView"] = ie;
            ie.__name__ = "com.watabou.mfcg.mapping.RoadsView";
            ie.smoothRoad = function(a, b) {
                a = Hf.render(a, !1, 3, b);
                for (var c = 0; c < b.length;) {
                    var d = b[c];
                    ++c;
                    ie.cutCorner(a, d, 1)
                }
                return a
            };
            ie.cutCorner = function(a, b, c) {
                var d = a.indexOf(b);
                if (-1 != d && 0 != d && d != a.length - 1) {
                    var f = a[d - 1],
                        h = a[d + 1],
                        k = I.distance(f, b),
                        n = I.distance(b, h);
                    k <= c / 2 || n <= c / 2 || (f = qa.lerp(f, b, (k - c) / k), b = qa.lerp(h, b, (n - c) / n), a.splice(d, 1), a.splice(d, 0, b), a.splice(d, 0, f))
                }
            };
            ie.getSegments = function(a, b) {
                for (var c = [], d = null, f = 0, h = a.length; f < h;) {
                    var k = f++,
                        n = a[k]; - 1 != b.edges.indexOf(n) || -1 != b.edges.indexOf(n.twin) ? null == d ? (d = [n], c.push(d), 0 < k && d.splice(0, 0,
                        a[k - 1])) : d.push(n) : (null != d && d.push(n), d = null)
                }
                return c
            };
            ie.__super__ = ka;
            ie.prototype = v(ka.prototype, {
                update: function(a) {
                    this.outline.removeChildren();
                    this.outline.get_graphics().clear();
                    this.fill.removeChildren();
                    this.fill.get_graphics().clear();
                    var b = "Hidden" != ba.get("display_mode", "Lots"),
                        c = ba.get("outline_roads", !0) ? K.getStrokeWidth(K.strokeNormal, !0) : 0;
                    if (null == a.focus)
                        for (var d = 0, f = a.arteries; d < f.length;) {
                            var h = f[d];
                            ++d;
                            this.drawRoad(h, c, b)
                        } else
                            for (d = 0, f = a.arteries; d < f.length;) {
                                h = f[d];
                                ++d;
                                for (var k = 0, n = ie.getSegments(h, a.focus); k < n.length;) h = n[k], ++k, this.drawRoad(h, c, b)
                            }
                    if (K.colorRoad != K.colorPaper)
                        for (h = this.fill.get_graphics(), d = 0, f = a.cells; d < f.length;) k = f[d], ++d, k.ward instanceof he && (k = va.__cast(k.ward, he).space, h.beginFill(K.colorRoad), h.lineStyle(2, K.colorRoad), Kb.drawPolygon(h, k), h.endFill());
                    if (ba.get("show_alleys", !1) && !sb.preview)
                        for (h = 0 < c ? this.drawOutline(c) : this.drawFill(.6), d = 0, f = a.districts; d < f.length;)
                            for (n = f[d], ++d, k = 0, n = n.groups; k < n.length;) {
                                var p = n[k];
                                ++k;
                                if (null ==
                                    a.focus || Z.intersects(p.faces, a.focus.faces))
                                    if (!p.urban || 0 == c || !b) {
                                        for (var g = 0, q = p.alleys; g < q.length;) {
                                            var m = q[g];
                                            ++g;
                                            Kb.drawPolyline(h, m)
                                        }
                                        g = 0;
                                        for (p = p.border; g < p.length;) m = p[g], ++g, null == m.data && m.twin.face.data.ward instanceof Pc && (q = m.origin.point, m = m.next.origin.point, h.moveTo(q.x, q.y), h.lineTo(m.x, m.y))
                                    }
                            }
                },
                drawRoad: function(a, b, c) {
                    for (var d = Ua.toPolyline(a), f = [], h = [], k = 0, n = a.length; k < n;) {
                        for (var p = k++, g = a[p].origin, q = !0, m = !0, u = 0, r = g.edges; u < r.length;) {
                            var l = r[u];
                            ++u;
                            l.face.data.withinCity ? m = !1 : q = !1
                        }
                        m || f.push(g.point);
                        q || h.push(p)
                    }
                    k = ie.smoothRoad(d, f);
                    a = this.drawFill(2);
                    Kb.drawPolyline(a, k);
                    if (0 < b)
                        if (a = this.drawOutline(2 + 2 * b), c) {
                            b = [];
                            k = 0;
                            for (n = h.length; k < n;) p = k++, 0 != p && h[p] - 1 == h[p - 1] || b.push([]), b[b.length - 1].push(h[p]);
                            for (k = 0; k < b.length;) {
                                h = b[k];
                                ++k;
                                0 < h[0] && h.unshift(h[0] - 1);
                                h[h.length - 1] < d.length - 1 && h.push(h[h.length - 1] + 1);
                                n = [];
                                for (u = 0; u < h.length;) p = h[u], ++u, n.push(d[p]);
                                h = ie.smoothRoad(n, f);
                                Kb.drawPolyline(a, h)
                            }
                        } else Kb.drawPolyline(a, k)
                },
                drawOutline: function(a) {
                    var b = new ka;
                    this.outline.addChild(b);
                    b = b.get_graphics();
                    b.lineStyle(a, K.colorDark, null, null, null, 0);
                    return b
                },
                drawFill: function(a) {
                    var b = new ka;
                    this.fill.addChild(b);
                    b = b.get_graphics();
                    b.lineStyle(a, K.colorRoad);
                    return b
                },
                __class__: ie
            });
            var K = function() {};
            g["com.watabou.mfcg.mapping.Style"] = K;
            K.__name__ = "com.watabou.mfcg.mapping.Style";
            K.getStrokeWidth = function(a, b) {
                null == b && (b = !0);
                K.thinLines && (a /= 3);
                b && (a *= K.lineInvScale);
                return a
            };
            K.getTint = function(a, b, c) {
                switch (K.tintMethod) {
                    case "Brightness":
                        return K.brightness(a, b, c);
                    case "Spectrum":
                        return K.spectrum(a,
                            b, c);
                    default:
                        return K.overlay(a, b, c)
                }
            };
            K.spectrum = function(a, b, c) {
                a = Gc.rgb2hsv(a);
                return Gc.hsv(a.x - 360 * (c - 1) / c * K.tintStrength / 100 * (b / (c - 1) - .5), a.y, a.z)
            };
            K.brightness = function(a, b, c) {
                a = Gc.rgb2hsv(a);
                return Gc.hsv(a.x, a.y, a.z + Math.min(a.z, 1 - a.z) * K.tintStrength / 50 * (b / (c - 1) - .5))
            };
            K.overlay = function(a, b, c) {
                var d = Gc.rgb2hsv(a);
                return Gc.lerp(a, Gc.hsv(d.x + 360 * b / c, d.y, d.z), K.tintStrength / 100)
            };
            K.setPalette = function(a, b) {
                null == b && (b = !1);
                null != a && (K.colorPaper = a.getColor("colorPaper"), K.colorLight = a.getColor("colorLight"),
                    K.colorDark = a.getColor("colorDark"), K.colorRoof = a.getColor("colorRoof", K.colorLight), K.colorWater = a.getColor("colorWater", K.colorPaper), K.colorGreen = a.getColor("colorGreen", K.colorPaper), K.colorRoad = a.getColor("colorRoad", K.colorPaper), K.colorWall = a.getColor("colorWall", K.colorDark), K.colorTree = a.getColor("colorTree", K.colorDark), K.colorLabel = a.getColor("colorLabel", K.colorDark), K.tintMethod = a.getString("tintMethod", K.tintMethods[0]), K.tintStrength = a.getInt("tintStrength", 50), K.weathering = a.getInt("weathering",
                        20), b && ba.set("colors", a.data()))
            };
            K.fillForm = function(a) {
                a.addTab("Colors");
                a.addColor("colorPaper", "Paper", K.colorPaper);
                a.addColor("colorDark", "Ink", K.colorDark);
                a.addColor("colorRoof", "Roofs", K.colorRoof);
                a.addColor("colorWater", "Water", K.colorWater);
                a.addColor("colorGreen", "Greens", K.colorGreen);
                a.addColor("colorRoad", "Roads", K.colorRoad);
                a.addColor("colorWall", "Walls", K.colorWall);
                a.addColor("colorTree", "Trees", K.colorTree);
                a.addColor("colorLabel", "Labels", K.colorLabel);
                a.addColor("colorLight",
                    "Elements", K.colorLight);
                a.addTab("Tints");
                a.addEnum("tintMethod", "Method", K.tintMethods, K.tintMethod);
                a.addInt("tintStrength", "Strength(%)", K.tintStrength, 0, 100);
                a.addInt("weathering", "Weathering(%)", K.weathering, 0, 100)
            };
            K.restore = function() {
                K.thinLines = ba.get("thin_lines", !0) || sb.preview;
                var a = ba.get("colors");
                null != a && K.setPalette(Xc.fromData(a), !1)
            };
            var gc = function(a) {
                var b = this;
                ka.call(this);
                this.trees = [];
                this.treeGroups = [];
                this.addCityTrees(a);
                ba.get("show_forests", !1) && this.addForests(a);
                var c =
                    ba.get("outline_trees", !0);
                a = function(a, d) {
                    var f = b.addLayer();
                    if (c) {
                        f.lineStyle(2 * K.getStrokeWidth(K.strokeNormal, !0), K.colorDark);
                        for (var h = 0; h < d.length;) {
                            var k = d[h];
                            ++h;
                            Kb.drawPolygon(f, k)
                        }
                        for (h = 0; h < a.length;) k = a[h], ++h, Kb.drawPolygonAt(f, k.crown, k.pos.x, k.pos.y);
                        f.endFill()
                    }
                    f.beginFill(K.colorTree);
                    for (h = 0; h < d.length;) k = d[h], ++h, Kb.drawPolygon(f, k);
                    for (h = 0; h < a.length;) k = a[h], ++h, f.beginFill(K.colorTree), Kb.drawPolygonAt(f, k.crown, k.pos.x, k.pos.y)
                };
                a(this.trees, []);
                for (var d = 0, f = this.treeGroups; d <
                    f.length;) {
                    var h = f[d];
                    ++d;
                    a(h.trees, h.polies)
                }
                this.mouseChildren = this.mouseEnabled = !1
            };
            g["com.watabou.mfcg.mapping.TreesLayer"] = gc;
            gc.__name__ = "com.watabou.mfcg.mapping.TreesLayer";
            gc.getForestFaces = function(a) {
                if (null == a.focus) return a.dcel.faces;
                var b = [],
                    c = 0;
                for (a = a.focus.faces; c < a.length;) {
                    var d = a[c];
                    ++c;
                    d = d.getNeighbours();
                    for (var f = 0; f < d.length;) {
                        var h = d[f];
                        ++f;
                        Z.add(b, h);
                        Z.addAll(b, h.getNeighbours())
                    }
                }
                return b
            };
            gc.getForestOutlines = function(a) {
                for (var b = [], c = [], d = 0; d < a.length;) {
                    var f = a[d];
                    ++d;
                    f.data.ward instanceof og && c.push(f)
                }
                for (; 0 < c.length;) a = Ic.floodFillEx(c[0], function(a) {
                    return -1 != c.indexOf(a.twin.face) ? null != a.data ? a.data == Tc.ROAD : !0 : !1
                }), b.push(Ic.outline(a)), Z.removeAll(c, a);
                return b
            };
            gc.getTree = function() {
                if (20 <= gc.cache.length) return Z.random(gc.cache);
                var a = 1.5 * Math.pow(1.5, ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3 * 2 - 1);
                a = gc.getCrown(a);
                gc.cache.push(a);
                return a
            };
            gc.getCrown =
                function(a) {
                    for (var b = [], c = 0; 6 > c;) {
                        var d = c++;
                        d = 2 * Math.PI * (d + ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3) / 6;
                        b.push(I.polar(a * (1 - 4 / 6 * Math.abs(((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 2 - 1)), d))
                    }
                    return mf.bloat(b, a * Math.sin(3 * Math.PI / 6))
                };
            gc.resetForests = function() {
                gc.savedKey =
                    null
            };
            gc.__super__ = ka;
            gc.prototype = v(ka.prototype, {
                addLayer: function() {
                    var a = new md;
                    this.addChild(a);
                    return a.get_graphics()
                },
                addCityTrees: function(a) {
                    if (null == a.focus) {
                        var b = 0;
                        for (a = a.cells; b < a.length;) {
                            var c = a[b];
                            ++b;
                            this.addTrees(c.ward.spawnTrees())
                        }
                    } else
                        for (b = 0, a = a.focus.faces; b < a.length;) c = a[b], ++b, this.addTrees(c.data.ward.spawnTrees())
                },
                addForests: function(a) {
                    var b = null == a.focus ? a.dcel.faces : a.focus.faces;
                    if (b == gc.savedKey) this.treeGroups = gc.savedGroups;
                    else {
                        for (var c = gc.getForestOutlines(gc.getForestFaces(a)),
                                d = [], f = 0; f < c.length;) {
                            var h = c[f];
                            ++f;
                            for (var k = [], n = 0; n < h.length;) {
                                var p = h[n];
                                ++n;
                                var g = Ua.toPoly(p);
                                g = uc.simpleInset(g, this.getForestInsets(a, p));
                                g = uc.resampleClosed(g, 20);
                                g = uc.fractalizeClosed(g, 2, .5);
                                g = Me.smoothClosed(g, 5);
                                k.push(g)
                            }
                            d.push({
                                trees: [],
                                polies: k
                            })
                        }
                        this.treeGroups = d;
                        a = a.getViewport().clone();
                        a.inflate(40, 40);
                        d = 0;
                        for (f = this.treeGroups; d < f.length;)
                            for (c = f[d], ++d, k = 0, n = c.polies; k < n.length;)
                                for (g = n[k], ++k, g = uc.resampleClosed(g, 3), h = 0; h < g.length;)
                                    if (p = g[h], ++h, a.containsPoint(p)) {
                                        var q =
                                            I.polar(1.5 * ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647), 2 * Math.PI * ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647));
                                        p.x += q.x;
                                        p.y += q.y;
                                        c.trees.push({
                                            pos: p,
                                            crown: gc.getTree()
                                        })
                                    } gc.savedKey = b;
                        gc.savedGroups = this.treeGroups
                    }
                },
                getForestInsets: function(a, b) {
                    for (var c = [], d = 0; d < b.length;) {
                        var f = b[d];
                        ++d;
                        null == f ? c.push(0) : f.data == Tc.CANAL ? c.push((20 + a.getCanalWidth(f)) / 2) : c.push(10)
                    }
                    return c
                },
                addTrees: function(a) {
                    if (null != a)
                        for (var b = 0; b < a.length;) {
                            var c = a[b];
                            ++b;
                            this.trees.push({
                                pos: c,
                                crown: gc.getTree()
                            })
                        }
                },
                __class__: gc
            });
            var ng = function() {
                ka.call(this);
                this.towers = ba.get("towers", "Round");
                this.outline = ba.get("outline_solids", !0) && K.colorWall != K.colorDark;
                this.stroke = K.getStrokeWidth(K.strokeNormal, !0)
            };
            g["com.watabou.mfcg.mapping.WallsView"] = ng;
            ng.__name__ = "com.watabou.mfcg.mapping.WallsView";
            ng.__super__ = ka;
            ng.prototype = v(ka.prototype, {
                draw: function(a, b, c) {
                    for (var d = pc.TOWER_RADIUS, f = a.edges.length, h = new pa, k = function(b, d) {
                            if (null == c || -1 != c.vertices.indexOf(b)) {
                                var k = b.point,
                                    n = Ua.indexByOrigin(a.edges,
                                        b),
                                    p = a.edges[(n + f - 1) % f];
                                n = a.edges[n];
                                var m = p.next.origin.point.subtract(p.origin.point),
                                    g = -d / 2;
                                null == g && (g = 1);
                                m = m.clone();
                                m.normalize(g);
                                p = k.add(m);
                                m = n.next.origin.point.subtract(n.origin.point);
                                g = d / 2;
                                null == g && (g = 1);
                                m = m.clone();
                                m.normalize(g);
                                d = k.add(m);
                                h.set(b, [p, d])
                            }
                        }, n = 0, p = a.gates; n < p.length;) {
                        var g = p[n];
                        ++n;
                        k(g, 2)
                    }
                    n = a.watergates;
                    for (p = n.keys(); p.hasNext();) {
                        g = p.next();
                        var q = n.get(g);
                        k(g, q.width)
                    }
                    k = this.get_graphics();
                    k.lineStyle(pc.THICKNESS, K.colorWall);
                    n = 0;
                    for (p = f; n < p;)
                        if (q = n++, g = a.edges[q],
                            a.segments[q] && (null == c || -1 != c.edges.indexOf(g) || -1 != c.edges.indexOf(g.twin))) {
                            var m = g.origin;
                            q = h.h[m.__id__];
                            if (null == q) q = m.point;
                            else {
                                m = g.next.origin.point.subtract(g.origin.point);
                                var u = d;
                                null == u && (u = 1);
                                m = m.clone();
                                m.normalize(u);
                                q = q[1].add(m)
                            }
                            u = g.next.origin;
                            m = h.h[u.__id__];
                            null == m ? g = u.point : (g = g.next.origin.point.subtract(g.origin.point), u = -d, null == u && (u = 1), g = g.clone(), g.normalize(u), g = m[0].add(g));
                            this.outline && (k.lineStyle(pc.THICKNESS + this.stroke, K.colorDark), k.moveTo(q.x, q.y), k.lineTo(g.x,
                                g.y), k.lineStyle(pc.THICKNESS - this.stroke, K.colorWall));
                            k.moveTo(q.x, q.y);
                            k.lineTo(g.x, g.y)
                        } b = b ? pc.LTOWER_RADIUS : d;
                    n = 0;
                    for (p = a.towers; n < p.length;) d = p[n], ++n, null != c && -1 == c.vertices.indexOf(d) || this.drawNormalTower(a, d.point, b);
                    for (g = h.iterator(); g.hasNext();) b = g.next(), this.drawGate(b)
                },
                drawNormalTower: function(a, b, c) {
                    var d = !0;
                    if ("Round" != this.towers) {
                        var f = a.shape;
                        var h = f.length,
                            k = f.indexOf(b);
                        a.bothSegments(k) ? (d = f[(k + 1) % h], f = f[(k + h - 1) % h].subtract(b), f.normalize(1), h = d.subtract(b), h.normalize(1),
                            d = 0 > f.x * h.y - f.y * h.x, f = f.add(h), h = d ? -1 : 1, null == h && (h = 1), f = f.clone(), f.normalize(h)) : (a.segments[k] ? f = f[k < f.length - 1 ? k + 1 : 0].subtract(f[k]) : (h = (k + h - 1) % h, f = f[h < f.length - 1 ? h + 1 : 0].subtract(f[h])), h = !0, null == h && (h = !1), f.setTo(f.y, -f.x), h && f.normalize(1))
                    } else f = ng.drawNormalTower_unit;
                    this.drawTower(b, f, d, c)
                },
                drawTower: function(a, b, c, d) {
                    c = new ka;
                    var f = c.get_graphics();
                    f.beginFill(K.colorWall);
                    this.outline && f.lineStyle(this.stroke, K.colorDark);
                    switch (this.towers) {
                        case "Open":
                            d *= .7;
                            var h = [],
                                k = I.polar(d, -4 *
                                    Math.PI / 8);
                            h.push(new I(k.x + .5 * d, k.y));
                            k = I.polar(d, -3 * Math.PI / 8);
                            h.push(new I(k.x + .5 * d, k.y));
                            k = I.polar(d, -2 * Math.PI / 8);
                            h.push(new I(k.x + .5 * d, k.y));
                            k = I.polar(d, -1 * Math.PI / 8);
                            h.push(new I(k.x + .5 * d, k.y));
                            k = I.polar(d, 0 * Math.PI / 8);
                            h.push(new I(k.x + .5 * d, k.y));
                            k = I.polar(d, Math.PI / 8);
                            h.push(new I(k.x + .5 * d, k.y));
                            k = I.polar(d, 2 * Math.PI / 8);
                            h.push(new I(k.x + .5 * d, k.y));
                            k = I.polar(d, 3 * Math.PI / 8);
                            h.push(new I(k.x + .5 * d, k.y));
                            k = I.polar(d, 4 * Math.PI / 8);
                            h.push(new I(k.x + .5 * d, k.y));
                            h.push(new I(-1.5 * d, d));
                            h.push(new I(-1.5 *
                                d, -d));
                            Kb.drawPolygon(f, h);
                            break;
                        case "Square":
                            d *= .9;
                            f.drawRect(.5 * (d - pc.THICKNESS / 2) - d, -d, 2 * d, 2 * d);
                            break;
                        default:
                            f.drawCircle(0, 0, d)
                    }
                    c.set_rotation(Math.atan2(b.y, b.x) / Math.PI * 180);
                    c.set_x(a.x);
                    c.set_y(a.y);
                    this.addChild(c)
                },
                drawGate: function(a) {
                    var b = 2 * pc.TOWER_RADIUS,
                        c = a[1].subtract(a[0]),
                        d = c.get_length();
                    a = a[0];
                    a = new I(a.x + .5 * c.x, a.y + .5 * c.y);
                    c = Math.atan2(c.y, c.x);
                    var f = new ka,
                        h = f.get_graphics();
                    h.beginFill(K.colorWall);
                    this.outline && h.lineStyle(this.stroke, K.colorDark);
                    h.drawRect(-d / 2, -b / 2, -b,
                        b);
                    h.drawRect(d / 2, -b / 2, b, b);
                    f.set_rotation(c / Math.PI * 180);
                    f.set_x(a.x);
                    f.set_y(a.y);
                    this.addChild(f)
                },
                __class__: ng
            });
            var Fd = function(a, b) {
                this.export = this.style = null;
                this.coastDir = NaN;
                this.size = a;
                this.seed = b
            };
