/*
 * mfcg-model/02-export.js
 * Part 2/8: Export Functionality
 * Contains: Export, JsonExporter, SvgExporter
 */
            g["com.watabou.mfcg.export.Export"] = Export;
            Export.__name__ = "com.watabou.mfcg.export.Export";
            Export.asPNG = function() {
                var a = City.instance,
                    b = a.getViewport(),
                    c = 40 * (null != a.focus ? .25 : 1),
                    d = b.width + c,
                    f = b.height + c;
                b = GraphicsUtils.getMaxArea(TownScene.map);
                c = Math.sqrt(Math.min(16777216 / (d * f), 67108864 / b));
                var h = c * d | 0,
                    k = c * f | 0;
                b = new Fb(h, k, !1, K.colorPaper);
                hb.trace(c, {
                    fileName: "Source/com/watabou/mfcg/export/Export.hx",
                    lineNumber: 36,
                    className: "com.watabou.mfcg.export.Export",
                    methodName: "asPNG",
                    customParams: [h, k]
                });
                c = TownScene.inst;
                k = c.rWidth;
                var n = c.rHeight;
                d *= c.get_mapScale();
                f *= c.get_mapScale();
                c.setSize(d, f);
                f = h / d;
                d = new ua;
                d.scale(f, f);
                f = TownScene.map;
                h = 0;
                for (var p = c.overlays; h < p.length;) {
                    var g = p[h];
                    ++h;
                    g.exportPNG(!0)
                }
                var q = f.get_transform().get_matrix().clone();
                q.concat(d);
                b.draw(f, q, null, null, null, !0);
                h = 0;
                for (p = c.overlays; h < p.length;) g = p[h], ++h, g.get_visible() && (q = g.get_transform().get_matrix().clone(), q.concat(d), b.draw(g, q, null, null, null, !0));
                c.setSize(k, n);
                c.resetOverlays();
                Exporter.savePNG(b, a.name)
            };
            Export.asSVG = function() {
                var a = City.instance,
                    b = SvgExporter.export(a, TownScene.inst);
                a = a.name;
                Exporter.saveText('<?xml version="1.0" encoding="UTF-8" standalone="no"?>' + Df.print(b.root), "" + a + ".svg", "image/svg+xml")
            };
            Export.asJSON = function() {
                var a = City.instance,
                    b = JsonExporter.export(a);
                a = a.name;
                Exporter.saveText(b.stringify(), "" + a + ".json", "application/json")
            };
            var JsonExporter = function() {};
            g["com.watabou.mfcg.export.JsonExporter"] = JsonExporter;
            JsonExporter.__name__ = "com.watabou.mfcg.export.JsonExporter";
            JsonExporter.export = function(a) {
                GeoJSON.SCALE = 4;
                for (var b = [], c = [], d = [],
                        f = [], h = [], k = new pa, n = 0, p = a.cells; n < p.length;) {
                    var g = p[n];
                    ++n;
                    var q = g.ward;
                    switch (va.getClass(q)) {
                        case Alleys:
                            if (q.group.core == g.face) {
                                var m = q.group.blocks;
                                switch (State.get("display_mode", "Lots")) {
                                    case "Block":
                                        g = [];
                                        for (var u = 0; u < m.length;) q = m[u], ++u, g.push(q.shape);
                                        m = g;
                                        for (var r = 0; r < m.length;) u = m[r], ++r, b.push(u);
                                        break;
                                    case "Complex":
                                        for (var l = [], D = 0; D < m.length;) q = m[D], ++D, l.push(q.buildings);
                                        m = Z.collect(l);
                                        for (q = 0; q < m.length;) {
                                            var x = m[q];
                                            ++q;
                                            b.push(x)
                                        }
                                        break;
                                    case "Lots":
                                        q = [];
                                        for (x = 0; x < m.length;) g = m[x], ++x,
                                            q.push(g.lots);
                                        m = Z.collect(q);
                                        for (q = 0; q < m.length;) x = m[q], ++q, b.push(x);
                                        break;
                                    case "Simple":
                                        q = [];
                                        for (x = 0; x < m.length;) g = m[x], ++x, q.push(g.rects);
                                        m = Z.collect(q);
                                        for (q = 0; q < m.length;) x = m[q], ++q, b.push(x)
                                }
                            }
                            break;
                        case Castle:
                            b.push(va.__cast(q, Castle).building);
                            break;
                        case Cathedral:
                            m = va.__cast(q, Cathedral).building;
                            for (q = 0; q < m.length;) x = m[q], ++q, b.push(x);
                            break;
                        case Farm:
                            m = va.__cast(q, Farm);
                            q = m.buildings;
                            for (x = 0; x < q.length;) g = q[x], ++x, b.push(g);
                            m = m.subPlots;
                            for (q = 0; q < m.length;) x = m[q], ++q, h.push(x);
                            break;
                        case Harbour:
                            m = 0;
                            for (q = va.__cast(q,
                                    Harbour).piers; m < q.length;) x = q[m], ++m, k.set(x, 1.2);
                            break;
                        case Market:
                            m = va.__cast(q, Market);
                            d.push(m.getAvailable());
                            null != m.monument && c.push(m.monument);
                            break;
                        case Park:
                            f.push(va.__cast(q, Park).green)
                    }
                }
                n = [];
                p = 0;
                for (g = a.districts; p < g.length;) r = g[p], ++p, m = GeoJSON.polygon(null, EdgeChain.toPoly(r.border)), u = new Qa, u.h.name = r.name, m.props = u, n.push(m);
                m = GeoJSON.geometryCollection("districts", n);
                n = new Qa;
                n.h.id = "values";
                n.h.roadWidth = 2 * GeoJSON.SCALE;
                n.h.towerRadius = CurtainWall.TOWER_RADIUS * GeoJSON.SCALE;
                n.h.wallThickness = CurtainWall.THICKNESS * GeoJSON.SCALE;
                n.h.generator =
                    "mfcg";
                n.h.version = A.current.meta.h.version;
                q = n;
                0 < a.canals.length && (q.h.riverWidth = a.canals[0].width * GeoJSON.SCALE);
                n = 0;
                for (p = a.canals; n < p.length;)
                    for (x = p[n], ++n, u = x.bridges.keys(); u.hasNext();) {
                        var w = u.next();
                        null == x.bridges.h[w.__id__] && (r = x.width + 1.2, l = x.course, D = EdgeChain.indexByOrigin(l, w), g = l[D], g = g.next.origin.point.subtract(g.origin.point), 0 < D && (l = l[D - 1], l = l.next.origin.point.subtract(l.origin.point), g.x += l.x, g.y += l.y), g = new I(-g.y, g.x), r /= 2, null == r && (r = 1), g = g.clone(), g.normalize(r), g = [w.point.subtract(g),
                            w.point.add(g)
                        ], k.set(g, 1.2))
                    }
                x = [];
                if (State.get("show_trees"))
                    for (n = 0, p = a.cells; n < p.length;)
                        if (g = p[n], ++n, w = g.ward.spawnTrees(), null != w)
                            for (g = 0; g < w.length;) u = w[g], ++g, x.push(u);
                n = [];
                p = 0;
                for (g = a.arteries; p < g.length;) D = g[p], ++p, w = GeoJSON.lineString(null, EdgeChain.toPolyline(D)), u = new Qa, u.h.width = 2 * GeoJSON.SCALE, w.props = u, n.push(w);
                w = n;
                if (State.get("show_alleys", !1))
                    for (n = 0, p = a.districts; n < p.length;)
                        for (r = p[n], ++n, g = 0, u = r.groups; g < u.length;)
                            for (l = u[g], ++g, r = 0, l = l.alleys; r < l.length;) {
                                D = l[r];
                                ++r;
                                var z = GeoJSON.lineString(null, D);
                                D = new Qa;
                                D.h.width = 1.2 * GeoJSON.SCALE;
                                z.props = D;
                                w.push(z)
                            }
                p = GeoJSON.feature(null, q);
                q = GeoJSON.polygon("earth", EdgeChain.toPoly(a.earthEdgeE));
                u = GeoJSON.geometryCollection("roads", w);
                w = JsonExporter.multiThick("walls", a.walls, !0, function(a) {
                    return a.shape
                }, function(a) {
                    return CurtainWall.THICKNESS
                });
                r = JsonExporter.multiThick("rivers", a.canals, null, function(a) {
                    return EdgeChain.toPolyline(a.course)
                }, function(a) {
                    return a.width
                });
                n = [];
                for (g = k.keys(); g.hasNext();) l = g.next(), n.push(l);
                b = GeoJSON.featureCollection([p, q, u, w, r, JsonExporter.multiThick("planks", n, null, function(a) {
                        return a
                    },
                    function(a) {
                        return k.h[a.__id__]
                    }), GeoJSON.multiPolygon("buildings", b), GeoJSON.multiPolygon("prisms", c), GeoJSON.multiPolygon("squares", d), GeoJSON.multiPolygon("greens", f), GeoJSON.multiPolygon("fields", h), GeoJSON.multiPoint("trees", x), m]);
                0 < a.waterEdgeE.length && b.items.push(GeoJSON.multiPolygon("water", [EdgeChain.toPoly(a.waterEdgeE)]));
                return b
            };
            JsonExporter.multiThick = function(a, b, c, d, f) {
                null == c && (c = !1);
                for (var h = [], k = 0; k < b.length;) {
                    var n = b[k];
                    ++k;
                    var p = d(n);
                    p = c ? GeoJSON.polygon(null, p) : GeoJSON.lineString(null, p);
                    var g = new Qa;
                    n = f(n) * GeoJSON.SCALE;
                    g.h.width = n;
                    p.props =
                        g;
                    h.push(p)
                }
                return GeoJSON.geometryCollection(a, h)
            };
            var SvgExporter = function() {};
            g["com.watabou.mfcg.export.SvgExporter"] = SvgExporter;
            SvgExporter.__name__ = "com.watabou.mfcg.export.SvgExporter";
            SvgExporter.export = function(a, b) {
                Sprite2SVG.substituteFont = SvgExporter.fixFontNames;
                Sprite2SVG.handleObject = SvgExporter.detectEmblem;
                var c = a.getViewport(),
                    d = 40 * (null != a.focus ? .25 : 1);
                a = c.height + d;
                d = (c.width + d) * b.get_mapScale();
                var f = a * b.get_mapScale();
                c = b.rWidth;
                a = b.rHeight;
                b.setSize(d, f);
                d = Sprite2SVG.create(d, f, K.colorPaper);
                f = Sprite2SVG.drawSprite(b);
                SVG.clearTransform(f);
                var h = Sprite2SVG.getImports();
                d.root.addChild(h);
                h = Sprite2SVG.getGradients();
                d.root.addChild(h);
                d.root.addChild(f);
                b.setSize(c, a);
                return d
            };
            SvgExporter.fixFontNames = function(a) {
                if (!SvgExporter.embeddedScanned) {
                    SvgExporter.embeddedScanned = !0;
                    for (var b = new Qa, c = SvgExporter.embedded.h, d = Object.keys(c), f = d.length, h = 0; h < f;) {
                        var k = d[h++],
                            n = c[k];
                        k = ac.getFont(k).name; - 1 != n.name.indexOf(" ") && (n.name = "'" + n.name + "'");
                        b.h[k] = {
                            name: n.name,
                            url: n.url,
                            generic: n.generic
                        }
                    }
                    SvgExporter.embedded = b
                }
                n = SvgExporter.embedded.h[a];
                return null != n ? (Sprite2SVG.addImport(n.url), n.name + ", " + n.generic) : Sprite2SVG.substituteGenerics(a)
            };
            SvgExporter.detectEmblem =
                function(a) {
                    return a instanceof Emblem && null != Emblem.svg ? Emblem.svg : null
                };
            var Namer = function() {};
