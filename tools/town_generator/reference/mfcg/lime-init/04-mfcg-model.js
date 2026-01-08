/*
 * lime-init/04-mfcg-model.js
 * Part 4/8: MFCG Core Model
 * Contains: com.watabou.mfcg.model.*, wards, blocks, districts
 */
            g["com.watabou.mfcg.Buffer"] = Buffer;
            Buffer.__name__ = "com.watabou.mfcg.Buffer";
            Buffer.write =
                function(a) {
                    E.navigator.clipboard.writeText(a)
                };
            var Values = function() {};
            g["com.watabou.mfcg.Values"] = Values;
            Values.__name__ = "com.watabou.mfcg.Values";
            Values.prepare = function(a) {
                a.random = !0;
                a.citadel = !0;
                a.urban_castle = !1;
                a.walls = !0;
                a.river = !1;
                a.coast = !0;
                a.temple = !0;
                a.plaza = !0;
                a.shantytown = !1;
                a.farms = !0;
                a.green = !1;
                a.hub = !1;
                a.gates = -1;
                a.display_mode = "Lots";
                a.lots_method = "Twisted";
                a.processing = "Offset";
                a.towers = "Round";
                a.landmarks = "Icon"
            };
            var Equator = function() {};
            g["com.watabou.mfcg.annotations.Equator"] = Equator;
            Equator.__name__ = "com.watabou.mfcg.annotations.Equator";
            Equator.build = function(a) {
                var b = PolyBounds.aabb(a),
                    c = b[0].subtract(b[3]);
                b = b[1].subtract(b[0]);
                var d = PolyCore.centroid(a);
                d = Equator.largestSpan(PolyCut.pierce(a, d, d.add(c)));
                c = d[0];
                d = d[1];
                for (var f = null, h = 0, k = 0, n = Equator.marks; k < n.length;) {
                    var p = n[k];
                    ++k;
                    p = GeomUtils.lerp(c, d, p);
                    p = Equator.largestSpan(PolyCut.pierce(a, p, p.add(b)));
                    var g = I.distance(p[0], p[1]);
                    h < g && (h = g, f = p)
                }
                return f
            };
            Equator.largestSpan = function(a) {
                for (var b = 0, c = null, d = null, f = 0; f < a.length;) {
                    var h = a[f],
                        k = a[f + 1],
                        n = I.distance(h, k);
                    b < n && (b = n, c = h, d = k);
                    f += 2
                }
                return [c, d]
            };
            var Xk = function() {};
            g["com.watabou.mfcg.annotations.Ridge"] =
                Xk;
            Xk.__name__ = "com.watabou.mfcg.annotations.Ridge";
            Xk.build = function(a) {
                for (var b = new SkeletonBuilder(a, !0); 0 < b.ribs.length;) {
                    for (var c = [], d = 0; d < a.length;) b = a[d], ++d, c.push(b.add(I.polar(1, 2 * Math.PI * ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647))));
                    a = c;
                    b = new SkeletonBuilder(a, !0)
                }
                var f = 0,
                    h = null,
                    k = null;
                c = 0;
                for (d = a.length - 1; c < d;)
                    for (var n = c++, p = n + 1, g = a.length; p < g;) {
                        var q = p++,
                            m = a[n];
                        q = a[q];
                        var u = I.distance(m, q) + Math.abs(m.x - q.x);
                        f < u && (f = u, h = m, k = q)
                    }
                a = b.leaves.h[h.__id__];
                k = b.leaves.h[k.__id__];
                c = [];
                d = 0;
                for (p = b.getPath(a, k); d <
                    p.length;) b = p[d], ++d, c.push(b.point);
                return c
            };
            var Export = function() {};
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
            g["com.watabou.mfcg.linguistics.Namer"] = Namer;
            Namer.__name__ = "com.watabou.mfcg.linguistics.Namer";
            Namer.reset = function() {
                if (null == Namer.grammar) {
                    Tracery.rng = C.float;
                    Namer.grammar = new Grammar(JSON.parse(ac.getText("grammar")));
                    Namer.grammar.defaultSelector = $h;
                    Namer.grammar.addModifiers(ModsEngBasic.get());
                    var a = Namer.grammar,
                        b = new Qa;
                    b.h.merge = Namer.merge;
                    a.addModifiers(b);
                    Namer.grammar.addExternal("word", Namer.word);
                    Namer.grammar.addExternal("fantasy", Namer.fantasy);
                    Namer.grammar.addExternal("given",
                        Namer.given);
                    Namer.english = new Markov(ac.getText("english").split(" "));
                    Namer.elven = new Markov(ac.getText("elven").split(" "));
                    Namer.male = new Markov(ac.getText("male").split(" "));
                    Namer.female = new Markov(ac.getText("female").split(" "))
                }
                Namer.grammar.clearState()
            };
            Namer.given = function() {
                return (.5 > (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 ? Namer.male : Namer.female).generate(4)
            };
            Namer.word = function() {
                return Namer.generate(Namer.english, 4, 3)
            };
            Namer.fantasy = function() {
                return Namer.generate(Namer.elven)
            };
            Namer.generate = function(a, b, c) {
                null == c && (c = 4);
                for (null == b &&
                    (b = 1);;) {
                    var d = a.generate(c);
                    if (d.length >= b && -1 == a.source.indexOf(d)) return d
                }
            };
            Namer.cityName = function(a) {
                var b = Namer.grammar,
                    c = a.bp.citadel;
                null == c && (c = !0);
                c ? Z.addAll(b.flags, (new ja(", +", "")).split("CASTLE")) : Z.removeAll(b.flags, (new ja(", +", "")).split("CASTLE"));
                b = Namer.grammar;
                c = a.bp.walls;
                null == c && (c = !0);
                c ? Z.addAll(b.flags, (new ja(", +", "")).split("WALLS")) : Z.removeAll(b.flags, (new ja(", +", "")).split("WALLS"));
                b = Namer.grammar;
                c = a.bp.temple;
                null == c && (c = !0);
                c ? Z.addAll(b.flags, (new ja(", +", "")).split("TEMPLE")) :
                    Z.removeAll(b.flags, (new ja(", +", "")).split("TEMPLE"));
                b = Namer.grammar;
                c = a.bp.plaza;
                null == c && (c = !0);
                c ? Z.addAll(b.flags, (new ja(", +", "")).split("PLAZA")) : Z.removeAll(b.flags, (new ja(", +", "")).split("PLAZA"));
                b = Namer.grammar;
                c = a.bp.shanty;
                null == c && (c = !0);
                c ? Z.addAll(b.flags, (new ja(", +", "")).split("SHANTY")) : Z.removeAll(b.flags, (new ja(", +", "")).split("SHANTY"));
                b = Namer.grammar;
                c = a.bp.coast;
                null == c && (c = !0);
                c ? Z.addAll(b.flags, (new ja(", +", "")).split("COAST")) : Z.removeAll(b.flags, (new ja(", +", "")).split("COAST"));
                b = Namer.grammar;
                c = a.bp.river;
                null == c && (c = !0);
                c ? Z.addAll(b.flags, (new ja(", +", "")).split("RIVER")) : Z.removeAll(b.flags, (new ja(", +", "")).split("RIVER"));
                return StringUtils.capitalizeAll(Namer.grammar.flatten("#city#"))
            };
            Namer.nameDistricts = function(a) {
                if (1 == a.districts.length) a.districts[0].name = a.name;
                else {
                    var b = 1 == a.name.split(" ").length ? a.name : "city";
                    Namer.grammar.pushRules("cityName", [null != b ? b : Namer.grammar.flatten("#cityName#")]);
                    for (var c = 0, d = a.districts; c < d.length;) {
                        var f = [d[c]];
                        ++c;
                        b = Namer.getDistrictNoun(f[0]);
                        Namer.grammar.pushRules("districtNoun", [null != b ? b : Namer.grammar.flatten("#districtNoun#")]);
                        b = Namer.getDirection(f[0]);
                        Namer.grammar.pushRules("dir", [null != b ? b : Namer.grammar.flatten("#dir#")]);
                        b = Namer.grammar;
                        var h = 1 == f[0].faces.length;
                        null == h && (h = !0);
                        h ? Z.addAll(b.flags, (new ja(", +", "")).split("COMPACT")) : Z.removeAll(b.flags, (new ja(", +", "")).split("COMPACT"));
                        b = Namer.grammar;
                        h = 1 == Z.count(a.districts, function(a) {
                            return function(b) {
                                return b.type == a[0].type
                            }
                        }(f));
                        null == h && (h = !0);
                        h ? Z.addAll(b.flags, (new ja(", +", "")).split("UNIQUE")) :
                            Z.removeAll(b.flags, (new ja(", +", "")).split("UNIQUE"));
                        switch (f[0].type._hx_index) {
                            case 0:
                                b = Namer.grammar.flatten("#centralDistrict#");
                                break;
                            case 1:
                                b = Namer.grammar.flatten("#castleDistrict#");
                                break;
                            case 2:
                                b = Namer.grammar.flatten("#docksDistrict#");
                                break;
                            case 3:
                                b = Namer.grammar.flatten("#bridgeDistrict#");
                                break;
                            case 4:
                                b = Namer.grammar.flatten("#gateDistrict#");
                                break;
                            case 5:
                                b = Namer.grammar.flatten("#bankDistrict#");
                                break;
                            case 6:
                                b = Namer.grammar.flatten("#parkDistrict#");
                                break;
                            case 7:
                                b = Namer.grammar.flatten("#sprawlDistrict#");
                                break;
                            default:
                                b = Namer.grammar.flatten("#district#")
                        }
                        Namer.grammar.popRules("dir");
                        Namer.grammar.popRules("districtNoun");
                        f[0].name = StringUtils.capitalizeAll(b)
                    }
                    Namer.grammar.popRules("cityName")
                }
            };
            Namer.getDistrictNoun = function(a) {
                a = a.faces.length + Math.floor((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * 3);
                return 2 >= a ? "quarter" : 6 > a ? "ward" : 12 > a ? "district" : "town"
            };
            Namer.getDirection = function(a) {
                var b = null,
                    c = -Infinity;
                null == a.equator && (a.equator = Equator.build(EdgeChain.toPoly(a.border)));
                a = GeomUtils.lerp(a.equator[0], a.equator[1]);
                for (var d = Namer.dirs,
                        f = d.keys(); f.hasNext();) {
                    var h = f.next(),
                        k = d.get(h);
                    h = h.x * a.x + h.y * a.y;
                    c < h && (c = h, b = k)
                }
                return Namer.grammar.flatten("#" + b + "#")
            };
            Namer.merge = function(a, b) {
                b = a.split(" ");
                if (1 == b.length) return a;
                for (var c = 0, d = 0; d < b.length;) {
                    var f = b[d];
                    ++d;
                    c += Syllables.split(f).length
                }
                return 2 >= c ? b.join("") : a
            };
            var BuildingPainter = function() {};
            g["com.watabou.mfcg.mapping.BuildingPainter"] = BuildingPainter;
            BuildingPainter.__name__ = "com.watabou.mfcg.mapping.BuildingPainter";
            BuildingPainter.paint = function(a, b, c, d, f, h) {
                null == h && (h = !1);
                null == f && (f = .5);
                if (h && PatchView.drawSolid) BuildingPainter.drawSolids(a, b);
                else {
                    PatchView.raised || (f = 0);
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
                                GraphicsExtender.drawPolygon(a, b.concat(c));
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
                    g = 1 < b.length && "Block" != PatchView.planMode && State.get("weathered_roofs", !1) ? K.weathering / 100 : 0;
                    for (f = 0; f < b.length;) h = b[f], ++f, q = 0 == g ? c : Color.scale(c, Math.pow(2, (((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3 * 2 - 1) * g)), m = BuildingPainter.outlineNormal ? d : q, a.beginFill(q), 0 != n ? (a.lineStyle(k, m), GraphicsExtender.drawPolygonAt(a, h, 0, n)) : (a.lineStyle(k, m, null, null,
                        null, null, 1), GraphicsExtender.drawPolygon(a, h)), a.endFill(), BuildingPainter.outlineNormal && "Block" != PatchView.planMode && BuildingPainter.drawRoof(a, h, n, k, m)
                }
            };
            BuildingPainter.drawSolids = function(a, b) {
                a.beginFill(K.colorWall);
                BuildingPainter.outlineSolid && K.colorDark != K.colorWall && a.lineStyle(K.getStrokeWidth(K.strokeNormal, !0), K.colorDark, null, null, null, null, 1);
                for (var c = 0; c < b.length;) {
                    var d = b[c];
                    ++c;
                    GraphicsExtender.drawPolygon(a, d)
                }
                a.endFill()
            };
            BuildingPainter.drawRoof = function(a, b, c, d, f) {
                if ("Plain" != PatchView.roofStyle && !Main.preview) {
                    for (var h = [], k = 0; k < b.length;) {
                        var n = b[k];
                        ++k;
                        h.push(n.add(I.polar(.1 *
                            (((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3 * 2 - 1), Math.PI * ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647))))
                    }
                    k = new SkeletonBuilder(h, !0);
                    if ("Gable" == PatchView.roofStyle) {
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
            var FarmPainter = function() {};
            g["com.watabou.mfcg.mapping.FarmPainter"] = FarmPainter;
            FarmPainter.__name__ = "com.watabou.mfcg.mapping.FarmPainter";
            FarmPainter.paint = function(a, b) {
                var c = State.get("farm_fileds", "Furrows");
                if ("Hidden" != c && !Main.preview) {
                    a = a.get_graphics();
                    var d = State.get("outline_fields", !1);
                    if ("Plain" == c) {
                        a.beginFill(K.colorGreen);
                        d && a.lineStyle(K.getStrokeWidth(K.strokeNormal, !0), K.colorDark);
                        c = 0;
                        for (var f = b.subPlots; c < f.length;) d = f[c], ++c, GraphicsExtender.drawPolygon(a, d)
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
                            for (c = 0, f = b.subPlots; c < f.length;) d = f[c], ++c, GraphicsExtender.drawPolygon(a, d)
                    }
                    a.endFill();
                    d = PatchView.planMode;
                    "Block" != d && "Hidden" != d && BuildingPainter.paint(a, b.buildings, K.colorRoof, K.colorDark, .3)
                }
            };
            var Focus = function(a) {
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
            g["com.watabou.mfcg.mapping.Focus"] = Focus;
            Focus.__name__ = "com.watabou.mfcg.mapping.Focus";
            Focus.district = function(a) {
                return new Focus(a.faces)
            };
            Focus.prototype = {
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
                __class__: Focus
            };
            var FocusView = function(a) {
                ka.call(this);
                if (null != a) {
                    var b = this.get_graphics(),
                        c = 5 * K.lineInvScale;
                    a = EdgeChain.toPoly(a.faces[0].data.district.border);
                    b.lineStyle(c, D.black, .4, !1, null, 0, 1);
                    GraphicsExtender.dashedPolyline(b, a, !0, [2 * c, 2 * c])
                }
            };
            g["com.watabou.mfcg.mapping.FocusView"] = FocusView;
            FocusView.__name__ = "com.watabou.mfcg.mapping.FocusView";
            FocusView.__super__ = ka;
            FocusView.prototype = v(ka.prototype, {
                __class__: FocusView
            });
            var FormalMap = function(a) {
                ka.call(this);
                this.model = a;
                PatchView.planMode = Main.preview ? "Lots" : State.get("display_mode", "Lots");
                PatchView.roofStyle = State.get("roof_style", "Plain");
                PatchView.raised = State.get("raised",
                    !0) && !Main.preview;
                PatchView.watercolours = State.get("watercolours", !1);
                PatchView.drawSolid = State.get("draw_solids", !0);
                BuildingPainter.outlineNormal = State.get("outline_buildings", !0) || Main.preview;
                BuildingPainter.outlineSolid = State.get("outline_solids", !0) || Main.preview;
                this.roads = new RoadsView;
                this.roads.update(a);
                this.addChild(this.roads);
                for (var b = 0, c = a.canals; b < c.length;) {
                    var d = c[b];
                    ++b;
                    var f = new RiverView;
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
                for (b = 0; b < c.length;) n = c[b], ++b, d = new PatchView(n), d.draw() && (this.patches.push(d), this.addChild(d));
                State.get("show_trees", !1) && "Block" != PatchView.planMode && !Main.preview && this.addChild(new TreesLayer(a));
                b = new WallsView;
                this.addChild(b);
                null != a.wall && b.draw(a.wall, !1, a.focus);
                null != a.citadel && b.draw(va.__cast(a.citadel.ward, Castle).wall, !0, a.focus);
                null != a.focus && this.addChild(new FocusView(a.focus));
                this.mouseEnabled = !1
            };
            g["com.watabou.mfcg.mapping.FormalMap"] = FormalMap;
            FormalMap.__name__ = "com.watabou.mfcg.mapping.FormalMap";
            FormalMap.__super__ = ka;
            FormalMap.prototype = v(ka.prototype, {
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
                        if (d instanceof TreesLayer) {
                            this.removeChild(d);
                            this.addChildAt(new TreesLayer(this.model), c);
                            break
                        }
                    }
                },
                drawWaterbody: function(a, b, c, d, f) {
                    for (var h = this.model.getOcean(), k = (b + c) / 2, n = (d + f) / 2, p = [], g = 0; g < h.length;) {
                        var q = h[g];
                        ++g; - 1 != this.model.horizon.indexOf(q) ? p.push(new I(q.x < k ? b : c, q.y < n ? d : f)) : p.push(new I(MathUtils.gate(q.x, b, c),
                            MathUtils.gate(q.y, d, f)))
                    }
                    h = p;
                    State.get("outline_water", !0) && a.lineStyle(K.getStrokeWidth(K.strokeNormal, !0), K.colorDark);
                    a.beginFill(K.colorWater);
                    GraphicsExtender.drawPolygon(a, h);
                    a.endFill()
                },
                __class__: FormalMap
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
            var PatchView = function(a) {
                md.call(this);
                this.patch = a;
                a.view = this;
                this.g = this.get_graphics()
            };
            g["com.watabou.mfcg.mapping.PatchView"] = PatchView;
            PatchView.__name__ = "com.watabou.mfcg.mapping.PatchView";
            PatchView.__super__ = md;
            PatchView.prototype = v(md.prototype, {
                draw: function() {
                    this.g.clear();
                    var a = va.getClass(this.patch.ward);
                    switch (a) {
                        case Alleys:
                        case Castle:
                        case Cathedral:
                        case Market:
                            var b = K.colorRoof,
                                c = K.colorDark;
                            if (PatchView.watercolours) {
                                var d = this.patch.ward.getColor();
                                b = d;
                                c = Color.lerp(c, d, .3)
                            }
                            switch (a) {
                                case Alleys:
                                    var f = va.__cast(this.patch.ward,
                                        Alleys).group;
                                    if (f.core == this.patch.face)
                                        for (a = 0, d = f.blocks; a < d.length;) {
                                            var h = d[a];
                                            ++a;
                                            var k = h == f.church;
                                            switch (PatchView.planMode) {
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
                                            BuildingPainter.paint(this.g, h, b, c, null, k)
                                        }
                                    break;
                                case Castle:
                                    K.colorRoad != K.colorPaper && (this.g.beginFill(K.colorRoad), GraphicsExtender.drawPolygon(this.g, this.patch.shape));
                                    a = va.__cast(this.patch.ward, Castle).building;
                                    BuildingPainter.paint(this.g, [a], b, c, 1, !0);
                                    break;
                                case Cathedral:
                                    a = va.__cast(this.patch.ward, Cathedral).building;
                                    BuildingPainter.paint(this.g, a, b, c, 1, !0);
                                    break;
                                case Market:
                                    a = va.__cast(this.patch.ward, Market), "Block" != PatchView.planMode && BuildingPainter.paint(this.g, [a.monument], b, c, 0, !0)
                            }
                            break;
                        case Farm:
                            FarmPainter.paint(this, this.patch.ward);
                            break;
                        case Harbour:
                            b = State.get("outline_water", !0);
                            a = 0;
                            for (d = va.__cast(this.patch.ward, Harbour).piers; a < d.length;) c = d[a], ++a, this.drawPier(c, b);
                            break;
                        case Park:
                            this.drawGreen(va.__cast(this.patch.ward, Park).green);
                            break;
                        default:
                            return !1
                    }
                    return !0
                },
                drawGreen: function(a) {
                    this.g.beginFill(K.colorGreen);
                    GraphicsExtender.drawPolygon(this.g, a)
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
                __class__: PatchView
            });
            var RiverView = function() {
                ka.call(this)
            };
            g["com.watabou.mfcg.mapping.RiverView"] = RiverView;
            RiverView.__name__ = "com.watabou.mfcg.mapping.RiverView";
            RiverView.getSegments = function(a, b) {
                for (var c = [], d = null, f = 0, h = a.length; f < h;) {
                    var k = f++,
                        n = a[k]; - 1 != b.edges.indexOf(n) || -1 != b.edges.indexOf(n.twin) ? null == d ? (d = [n], c.push(d), 0 < k && d.splice(0, 0, a[k - 1])) : d.push(n) : (null != d && d.push(n), d = null)
                }
                return c
            };
            RiverView.__super__ = ka;
            RiverView.prototype = v(ka.prototype, {
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
                        for (f = RiverView.getSegments(b.course, a.focus), d = 0; d < f.length;) k = f[d], ++d, k = EdgeChain.toPolyline(k), k = Chaikin.render(k, !1, 3, h), this.drawCourse(k, b.width);
                    else k = EdgeChain.toPolyline(b.course), c && (k[0] = GeomUtils.lerp(k[0], k[1])), k = Chaikin.render(k, !1, 3, h), this.drawCourse(k, b.width);
                    !c || null != a.focus && -1 == a.focus.vertices.indexOf(b.course[0].origin) || this.drawMouth(b);
                    c = (b.width + 1.2) / 2;
                    h = f = b.bridges;
                    for (f = f.keys(); f.hasNext();)
                        if (k = f.next(), d = h.get(k), n = k, k = d, null == a.focus || -1 != a.focus.vertices.indexOf(n)) {
                            d = n.point;
                            var p = EdgeChain.indexByOrigin(b.course, n),
                                g = b.course[p];
                            g = g.next.origin.point.subtract(g.origin.point);
                            0 < p && (p = b.course[p - 1], p = p.next.origin.point.subtract(p.origin.point), g.x += p.x, g.y += p.y);
                            if (null == k) k = new I(-g.y, g.x), n = c, null == n && (n = 1), k = k.clone(), k.normalize(n), this.drawBridge(d.subtract(k), d.add(k), 1.2);
                            else {
                                n = EdgeChain.indexByOrigin(k, n);
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
                    State.get("outline_water", !0) ? (c.lineStyle(b, K.colorDark, null, !1, null, 0), GraphicsExtender.drawPolyline(c, a), c.lineStyle(b - 2 * K.getStrokeWidth(K.strokeNormal, !0), K.colorWater)) : c.lineStyle(b, K.colorWater);
                    GraphicsExtender.drawPolyline(c, a);
                    c.endFill()
                },
                drawBridge: function(a, b, c) {
                    var d = this.get_graphics();
                    if (State.get("outline_roads", !0) || State.get("outline_water",
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
                    var g = GeomUtils.lerp(k.origin.point, k.next.origin.point,
                        .5);
                    n = new I(-p.y, p.x);
                    var q = c / 2;
                    null == q && (q = 1);
                    n = n.clone();
                    n.normalize(q);
                    a = g.add(n);
                    k = GeomUtils.lerp(f[(h + f.length - 1) % f.length], d);
                    var m = a.subtract(p),
                        u = GeomUtils.lerp(k, d);
                    n = new I(-p.y, p.x);
                    q = c / 2;
                    null == q && (q = 1);
                    n = n.clone();
                    n.normalize(q);
                    c = g.subtract(n);
                    p = c.subtract(p);
                    n = GeomUtils.lerp(f[(h + 1) % f.length], d);
                    g = GeomUtils.lerp(n, d);
                    q = this.get_graphics();
                    q.beginFill(K.colorWater);
                    q.moveTo(a.x, a.y);
                    q.cubicCurveTo(m.x, m.y, u.x, u.y, k.x, k.y);
                    PolyAccess.isConvexVertexi(f, h) && q.lineTo(d.x, d.y);
                    q.lineTo(n.x, n.y);
                    q.cubicCurveTo(g.x, g.y, p.x, p.y,
                        c.x, c.y);
                    q.endFill();
                    State.get("outline_water", !0) && (q.lineStyle(b, K.colorDark), q.moveTo(a.x, a.y), q.cubicCurveTo(m.x, m.y, u.x, u.y, k.x, k.y), q.moveTo(c.x, c.y), q.cubicCurveTo(p.x, p.y, g.x, g.y, n.x, n.y), q.moveTo(0, 0), q.endFill())
                },
                __class__: RiverView
            });
            var RoadsView = function() {
                ka.call(this);
                this.outline = new ka;
                this.addChild(this.outline);
                this.fill = new ka;
                this.addChild(this.fill)
            };
            g["com.watabou.mfcg.mapping.RoadsView"] = RoadsView;
            RoadsView.__name__ = "com.watabou.mfcg.mapping.RoadsView";
            RoadsView.smoothRoad = function(a, b) {
                a = Chaikin.render(a, !1, 3, b);
                for (var c = 0; c < b.length;) {
                    var d = b[c];
                    ++c;
                    RoadsView.cutCorner(a, d, 1)
                }
                return a
            };
            RoadsView.cutCorner = function(a, b, c) {
                var d = a.indexOf(b);
                if (-1 != d && 0 != d && d != a.length - 1) {
                    var f = a[d - 1],
                        h = a[d + 1],
                        k = I.distance(f, b),
                        n = I.distance(b, h);
                    k <= c / 2 || n <= c / 2 || (f = GeomUtils.lerp(f, b, (k - c) / k), b = GeomUtils.lerp(h, b, (n - c) / n), a.splice(d, 1), a.splice(d, 0, b), a.splice(d, 0, f))
                }
            };
            RoadsView.getSegments = function(a, b) {
                for (var c = [], d = null, f = 0, h = a.length; f < h;) {
                    var k = f++,
                        n = a[k]; - 1 != b.edges.indexOf(n) || -1 != b.edges.indexOf(n.twin) ? null == d ? (d = [n], c.push(d), 0 < k && d.splice(0, 0,
                        a[k - 1])) : d.push(n) : (null != d && d.push(n), d = null)
                }
                return c
            };
            RoadsView.__super__ = ka;
            RoadsView.prototype = v(ka.prototype, {
                update: function(a) {
                    this.outline.removeChildren();
                    this.outline.get_graphics().clear();
                    this.fill.removeChildren();
                    this.fill.get_graphics().clear();
                    var b = "Hidden" != State.get("display_mode", "Lots"),
                        c = State.get("outline_roads", !0) ? K.getStrokeWidth(K.strokeNormal, !0) : 0;
                    if (null == a.focus)
                        for (var d = 0, f = a.arteries; d < f.length;) {
                            var h = f[d];
                            ++d;
                            this.drawRoad(h, c, b)
                        } else
                            for (d = 0, f = a.arteries; d < f.length;) {
                                h = f[d];
                                ++d;
                                for (var k = 0, n = RoadsView.getSegments(h, a.focus); k < n.length;) h = n[k], ++k, this.drawRoad(h, c, b)
                            }
                    if (K.colorRoad != K.colorPaper)
                        for (h = this.fill.get_graphics(), d = 0, f = a.cells; d < f.length;) k = f[d], ++d, k.ward instanceof Market && (k = va.__cast(k.ward, Market).space, h.beginFill(K.colorRoad), h.lineStyle(2, K.colorRoad), GraphicsExtender.drawPolygon(h, k), h.endFill());
                    if (State.get("show_alleys", !1) && !Main.preview)
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
                                            GraphicsExtender.drawPolyline(h, m)
                                        }
                                        g = 0;
                                        for (p = p.border; g < p.length;) m = p[g], ++g, null == m.data && m.twin.face.data.ward instanceof Alleys && (q = m.origin.point, m = m.next.origin.point, h.moveTo(q.x, q.y), h.lineTo(m.x, m.y))
                                    }
                            }
                },
                drawRoad: function(a, b, c) {
                    for (var d = EdgeChain.toPolyline(a), f = [], h = [], k = 0, n = a.length; k < n;) {
                        for (var p = k++, g = a[p].origin, q = !0, m = !0, u = 0, r = g.edges; u < r.length;) {
                            var l = r[u];
                            ++u;
                            l.face.data.withinCity ? m = !1 : q = !1
                        }
                        m || f.push(g.point);
                        q || h.push(p)
                    }
                    k = RoadsView.smoothRoad(d, f);
                    a = this.drawFill(2);
                    GraphicsExtender.drawPolyline(a, k);
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
                                h = RoadsView.smoothRoad(n, f);
                                GraphicsExtender.drawPolyline(a, h)
                            }
                        } else GraphicsExtender.drawPolyline(a, k)
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
                __class__: RoadsView
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
                a = Color.rgb2hsv(a);
                return Color.hsv(a.x - 360 * (c - 1) / c * K.tintStrength / 100 * (b / (c - 1) - .5), a.y, a.z)
            };
            K.brightness = function(a, b, c) {
                a = Color.rgb2hsv(a);
                return Color.hsv(a.x, a.y, a.z + Math.min(a.z, 1 - a.z) * K.tintStrength / 50 * (b / (c - 1) - .5))
            };
            K.overlay = function(a, b, c) {
                var d = Color.rgb2hsv(a);
                return Color.lerp(a, Color.hsv(d.x + 360 * b / c, d.y, d.z), K.tintStrength / 100)
            };
            K.setPalette = function(a, b) {
                null == b && (b = !1);
                null != a && (K.colorPaper = a.getColor("colorPaper"), K.colorLight = a.getColor("colorLight"),
                    K.colorDark = a.getColor("colorDark"), K.colorRoof = a.getColor("colorRoof", K.colorLight), K.colorWater = a.getColor("colorWater", K.colorPaper), K.colorGreen = a.getColor("colorGreen", K.colorPaper), K.colorRoad = a.getColor("colorRoad", K.colorPaper), K.colorWall = a.getColor("colorWall", K.colorDark), K.colorTree = a.getColor("colorTree", K.colorDark), K.colorLabel = a.getColor("colorLabel", K.colorDark), K.tintMethod = a.getString("tintMethod", K.tintMethods[0]), K.tintStrength = a.getInt("tintStrength", 50), K.weathering = a.getInt("weathering",
                        20), b && State.set("colors", a.data()))
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
                K.thinLines = State.get("thin_lines", !0) || Main.preview;
                var a = State.get("colors");
                null != a && K.setPalette(Palette.fromData(a), !1)
            };
            var TreesLayer = function(a) {
                var b = this;
                ka.call(this);
                this.trees = [];
                this.treeGroups = [];
                this.addCityTrees(a);
                State.get("show_forests", !1) && this.addForests(a);
                var c =
                    State.get("outline_trees", !0);
                a = function(a, d) {
                    var f = b.addLayer();
                    if (c) {
                        f.lineStyle(2 * K.getStrokeWidth(K.strokeNormal, !0), K.colorDark);
                        for (var h = 0; h < d.length;) {
                            var k = d[h];
                            ++h;
                            GraphicsExtender.drawPolygon(f, k)
                        }
                        for (h = 0; h < a.length;) k = a[h], ++h, GraphicsExtender.drawPolygonAt(f, k.crown, k.pos.x, k.pos.y);
                        f.endFill()
                    }
                    f.beginFill(K.colorTree);
                    for (h = 0; h < d.length;) k = d[h], ++h, GraphicsExtender.drawPolygon(f, k);
                    for (h = 0; h < a.length;) k = a[h], ++h, f.beginFill(K.colorTree), GraphicsExtender.drawPolygonAt(f, k.crown, k.pos.x, k.pos.y)
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
            g["com.watabou.mfcg.mapping.TreesLayer"] = TreesLayer;
            TreesLayer.__name__ = "com.watabou.mfcg.mapping.TreesLayer";
            TreesLayer.getForestFaces = function(a) {
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
            TreesLayer.getForestOutlines = function(a) {
                for (var b = [], c = [], d = 0; d < a.length;) {
                    var f = a[d];
                    ++d;
                    f.data.ward instanceof Wilderness && c.push(f)
                }
                for (; 0 < c.length;) a = DCEL.floodFillEx(c[0], function(a) {
                    return -1 != c.indexOf(a.twin.face) ? null != a.data ? a.data == Tc.ROAD : !0 : !1
                }), b.push(DCEL.outline(a)), Z.removeAll(c, a);
                return b
            };
            TreesLayer.getTree = function() {
                if (20 <= TreesLayer.cache.length) return Z.random(TreesLayer.cache);
                var a = 1.5 * Math.pow(1.5, ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3 * 2 - 1);
                a = TreesLayer.getCrown(a);
                TreesLayer.cache.push(a);
                return a
            };
            TreesLayer.getCrown =
                function(a) {
                    for (var b = [], c = 0; 6 > c;) {
                        var d = c++;
                        d = 2 * Math.PI * (d + ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3) / 6;
                        b.push(I.polar(a * (1 - 4 / 6 * Math.abs(((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 2 - 1)), d))
                    }
                    return Bloater.bloat(b, a * Math.sin(3 * Math.PI / 6))
                };
            TreesLayer.resetForests = function() {
                TreesLayer.savedKey =
                    null
            };
            TreesLayer.__super__ = ka;
            TreesLayer.prototype = v(ka.prototype, {
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
                    if (b == TreesLayer.savedKey) this.treeGroups = TreesLayer.savedGroups;
                    else {
                        for (var c = TreesLayer.getForestOutlines(TreesLayer.getForestFaces(a)),
                                d = [], f = 0; f < c.length;) {
                            var h = c[f];
                            ++f;
                            for (var k = [], n = 0; n < h.length;) {
                                var p = h[n];
                                ++n;
                                var g = EdgeChain.toPoly(p);
                                g = PolyUtils.simpleInset(g, this.getForestInsets(a, p));
                                g = PolyUtils.resampleClosed(g, 20);
                                g = PolyUtils.fractalizeClosed(g, 2, .5);
                                g = Cubic.smoothClosed(g, 5);
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
                                for (g = n[k], ++k, g = PolyUtils.resampleClosed(g, 3), h = 0; h < g.length;)
                                    if (p = g[h], ++h, a.containsPoint(p)) {
                                        var q =
                                            I.polar(1.5 * ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647), 2 * Math.PI * ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647));
                                        p.x += q.x;
                                        p.y += q.y;
                                        c.trees.push({
                                            pos: p,
                                            crown: TreesLayer.getTree()
                                        })
                                    } TreesLayer.savedKey = b;
                        TreesLayer.savedGroups = this.treeGroups
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
                                crown: TreesLayer.getTree()
                            })
                        }
                },
                __class__: TreesLayer
            });
            var WallsView = function() {
                ka.call(this);
                this.towers = State.get("towers", "Round");
                this.outline = State.get("outline_solids", !0) && K.colorWall != K.colorDark;
                this.stroke = K.getStrokeWidth(K.strokeNormal, !0)
            };
            g["com.watabou.mfcg.mapping.WallsView"] = WallsView;
            WallsView.__name__ = "com.watabou.mfcg.mapping.WallsView";
            WallsView.__super__ = ka;
            WallsView.prototype = v(ka.prototype, {
                draw: function(a, b, c) {
                    for (var d = CurtainWall.TOWER_RADIUS, f = a.edges.length, h = new pa, k = function(b, d) {
                            if (null == c || -1 != c.vertices.indexOf(b)) {
                                var k = b.point,
                                    n = EdgeChain.indexByOrigin(a.edges,
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
                    k.lineStyle(CurtainWall.THICKNESS, K.colorWall);
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
                            this.outline && (k.lineStyle(CurtainWall.THICKNESS + this.stroke, K.colorDark), k.moveTo(q.x, q.y), k.lineTo(g.x,
                                g.y), k.lineStyle(CurtainWall.THICKNESS - this.stroke, K.colorWall));
                            k.moveTo(q.x, q.y);
                            k.lineTo(g.x, g.y)
                        } b = b ? CurtainWall.LTOWER_RADIUS : d;
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
                    } else f = WallsView.drawNormalTower_unit;
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
                            GraphicsExtender.drawPolygon(f, h);
                            break;
                        case "Square":
                            d *= .9;
                            f.drawRect(.5 * (d - CurtainWall.THICKNESS / 2) - d, -d, 2 * d, 2 * d);
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
                    var b = 2 * CurtainWall.TOWER_RADIUS,
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
                __class__: WallsView
            });
            var Blueprint = function(a, b) {
                this.export = this.style = null;
                this.coastDir = NaN;
                this.size = a;
                this.seed = b
            };
            g["com.watabou.mfcg.model.Blueprint"] = Blueprint;
            Blueprint.__name__ = "com.watabou.mfcg.model.Blueprint";
            Blueprint.create = function(a, b) {
                b = new Blueprint(a, b);
                b.name = null;
                b.pop = 0;
                b.greens = State.get("green", !1);
                b.random = State.get("random", !0);
                if (b.random) {
                    var c = (a + 30) / 80;
                    null == c && (c = .5);
                    b.walls = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 <
                        c;
                    c = a / 80;
                    null == c && (c = .5);
                    b.shanty = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < c;
                    c = .5 + a / 100;
                    null == c && (c = .5);
                    b.citadel = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < c;
                    c = b.walls ? a / (a + 30) : .5;
                    null == c && (c = .5);
                    b.inner = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < c;
                    c = .9;
                    null == c && (c = .5);
                    b.plaza = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < c;
                    c = a / 18;
                    null == c && (c = .5);
                    b.temple = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < c;
                    c = .6666666666666666;
                    null == c && (c = .5);
                    b.river = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 <
                        c;
                    b.coast = .5 > (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647
                } else b.citadel = State.get("citadel", !0), b.inner = State.get("urban_castle", !1), b.plaza = State.get("plaza", !0), b.temple = State.get("temple", !0), b.walls = State.get("walls", !0), b.shanty = State.get("shantytown", !1), b.coast = State.get("coast", !0), b.river = State.get("river", !0);
                b.hub = State.get("hub", !1);
                b.gates = State.get("gates", -1);
                return b
            };
            Blueprint.similar = function(a) {
                var b = Blueprint.create(a.size, a.seed);
                b.name = a.name;
                return b
            };
            Blueprint.fromURL = function() {
                var a = URLState.getInt("size", 0),
                    b = URLState.getInt("seed",
                        C.seed);
                if (0 == a || 0 == b) return null;
                a = new Blueprint(a, b);
                a.name = URLState.get("name");
                a.pop = URLState.getInt("population", 0);
                a.citadel = URLState.getFlag("citadel", !0);
                a.inner = URLState.getFlag("urban_castle", !1);
                a.plaza = URLState.getFlag("plaza", !0);
                a.temple = URLState.getFlag("temple", !0);
                a.walls = URLState.getFlag("walls", !0);
                a.shanty = URLState.getFlag("shantytown", !1);
                a.river = URLState.getFlag("river", !1);
                a.coast = URLState.getFlag("coast", !0);
                a.greens = URLState.getFlag("greens", !1);
                a.hub = URLState.getFlag("hub", !1);
                a.gates = URLState.getInt("gates", -1);
                a.coastDir = parseFloat(URLState.get("sea", "0.0"));
                a.export = URLState.get("export");
                a.style = URLState.get("style");
                return a
            };
            Blueprint.prototype = {
                updateURL: function() {
                    URLState.reset();
                    URLState.set("size", this.size);
                    URLState.set("seed", this.seed);
                    null != this.name && URLState.set("name", this.name);
                    0 < this.pop && URLState.set("population", this.pop);
                    URLState.setFlag("citadel", this.citadel);
                    URLState.setFlag("urban_castle", this.inner);
                    URLState.setFlag("plaza", this.plaza);
                    URLState.setFlag("temple", this.temple);
                    URLState.setFlag("walls", this.walls);
                    URLState.setFlag("shantytown", this.shanty);
                    URLState.setFlag("coast", this.coast);
                    URLState.setFlag("river", this.river);
                    URLState.setFlag("greens", this.greens);
                    this.hub ? URLState.setFlag("hub") : URLState.set("gates", this.gates);
                    this.coast && URLState.set("sea", this.coastDir)
                },
                __class__: Blueprint
            };
            var Building = function() {};
            g["com.watabou.mfcg.model.Building"] = Building;
            Building.__name__ = "com.watabou.mfcg.model.Building";
            Building.create = function(a, b, c, d, f) {
                null == f && (f = 0);
                null == d && (d = !1);
                null == c && (c = !1);
                var h = Math.sqrt(b);
                b = I.distance(a[0], a[1]);
                var k = I.distance(a[1], a[2]),
                    n = I.distance(a[2], a[3]),
                    p = I.distance(a[3], a[0]);
                b = Math.ceil(Math.min(b, n) / h);
                h = Math.ceil(Math.min(k, p) / h);
                if (1 >= b || 1 >= h) return null;
                c = d ? Building.getPlanSym(b, h) : c ? Building.getPlanFront(b, h) : Building.getPlan(b, h);
                for (d = k = 0; d < c.length;) p = c[d], ++d, p && ++k;
                if (k >= b * h) return null;
                a = Cutter.grid(a, b, h, f);
                d = [];
                f = 0;
                for (b = a.length; f < b;) h = f++, c[h] && d.push(a[h]);
                return Building.circumference(d)
            };
            Building.getPlan = function(a, b, c) {
                null == c && (c = .5);
                for (var d = a * b, f = [], h = 0, k = d; h < k;) h++, f.push(!1);
                var n = Math.floor((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * a),
                    p = Math.floor((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * b);
                f[n + p * a] = !0;
                --d;
                k = h = n;
                for (var g =
                        p, q = p;;) {
                    n = Math.floor((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * a);
                    p = Math.floor((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * b);
                    var m = n + p * a;
                    !f[m] && (0 < n && f[m - 1] || 0 < p && f[m - a] || n < a - 1 && f[m + 1] || p < b - 1 && f[m + a]) && (h > n && (h = n), k < n && (k = n), g > p && (g = p), q < p && (q = p), f[m] = !0, --d);
                    0 < h || k < a - 1 || 0 < g || q < b - 1 ? n = !0 : 0 < d ? (n = c, null == n && (n = .5), n = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < n) : n = !1;
                    if (!n) break
                }
                return f
            };
            Building.getPlanFront = function(a, b) {
                for (var c = a * b, d = [], f = 0, h = c; f < h;) {
                    var k = f++;
                    d.push(k < a)
                }
                c -= a;
                for (f = 0;;) {
                    h =
                        Math.floor((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * a);
                    k = Math.floor(1 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * (b - 1));
                    var n = h + k * a;
                    !d[n] && (0 < h && d[n - 1] || 0 < k && d[n - a] || h < a - 1 && d[n + 1] || k < b - 1 && d[n + a]) && (f < k && (f = k), d[n] = !0, --c);
                    f >= b - 1 ? 0 < c ? (h = .5, null == h && (h = .5), h = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < h) : h = !1 : h = !0;
                    if (!h) break
                }
                return d
            };
            Building.getPlanSym = function(a, b) {
                for (var c = Building.getPlan(a, b, 0), d = 0; d < b;)
                    for (var f = d++, h = 0, k = a; h < k;) {
                        var n = h++,
                            p = f * a + n;
                        n = (f + 1) * a - 1 - n;
                        c[p] = c[n] = c[p] || c[n]
                    }
                return c
            };
            Building.circumference = function(a) {
                if (0 == a.length) return [];
                if (1 == a.length) return a[0];
                for (var b = [], c = [], d = 0; d < a.length;) {
                    var f = a[d];
                    ++d;
                    for (var h = 0, k = f.length; h < k;) {
                        var n = h++,
                            p = f[n],
                            g = f[(n + 1) % f.length],
                            q = !0;
                        n = 0;
                        do
                            if (n = b.indexOf(g, n), -1 == n) break;
                            else if (c[n] == p) {
                            b.splice(n, 1);
                            c.splice(n, 1);
                            q = !1;
                            break
                        } else n += 1;
                        while (n < b.length);
                        q && (b.push(p), c.push(g))
                    }
                }
                d = f = 0;
                for (h = b.length; d < h;)
                    if (n = d++, b.lastIndexOf(b[n]) != n) {
                        f = n;
                        break
                    } d = b[f];
                h = c[f];
                a = [d];
                do a.push(h), h = c[b.indexOf(h)]; while (h != d);
                b = [];
                d = 0;
                for (h = a.length; d <
                    h;) n = d++, c = a[(n + 1) % a.length], f = a[(n + 2) % a.length], n = c.subtract(a[n]), f = f.subtract(c), .999 < (n.x * f.x + n.y * f.y) / n.get_length() / f.get_length() && b.push(c);
                for (d = 0; d < b.length;) f = b[d], ++d, N.remove(a, f);
                return a
            };
            var Canal = function(a, b) {
                var c = EdgeChain.toPolyline(b);
                if (0 < a.waterEdge.length) {
                    var d = c[0];
                    PointExtender.set(d, GeomUtils.lerp(d, c[1]));
                    var f = a.shore,
                        h = f.indexOf(d);
                    2 <= h && PointExtender.set(f[h - 1], GeomUtils.lerp(f[h - 1], GeomUtils.lerp(f[h - 2], d)));
                    h < f.length - 2 && PointExtender.set(f[h + 1], GeomUtils.lerp(f[h + 1], GeomUtils.lerp(f[h + 2], d)))
                }
                PolyCore.set(c, PolyUtils.smoothOpen(c, null, 1));
                EdgeChain.assignData(b,
                    Tc.CANAL);
                if (null != a.wall) {
                    d = a.wall.shape;
                    f = 1;
                    for (var k = b.length; f < k;) {
                        h = f++;
                        var n = c[h],
                            p = d.indexOf(n);
                        if (-1 != p) {
                            var g = c[h - 1],
                                q = c[h + 1].subtract(g),
                                m = d.length,
                                u = d[(p + m - 1) % m];
                            p = d[(p + 1) % m];
                            u = GeomUtils.intersectLines(g.x, g.y, q.x, q.y, u.x, u.y, p.x - u.x, p.y - u.y).x;
                            g = new I(g.x + q.x * u, g.y + q.y * u);
                            PointExtender.set(c[h], GeomUtils.lerp(n, g))
                        }
                    }
                }
                this.city = a;
                this.course = b
            };
            g["com.watabou.mfcg.model.Canal"] = Canal;
            Canal.__name__ = "com.watabou.mfcg.model.Canal";
            Canal.createRiver = function(a) {
                Canal.buildTopology(a);
                var b = 0 < a.shoreE.length ? Canal.deltaRiver(a) :
                    Canal.regularRiver(a);
                if (null != b) return new Canal(a, b);
                throw new Vb("Unable to build a canal!");
            };
            Canal.regularRiver = function(a) {
                for (var b = EdgeChain.vertices(a.horizonE), c = [], d = 0, f = b; d < f.length;) {
                    var h = f[d];
                    ++d;
                    1 < a.cellsByVertex(h).length && c.push(h)
                }
                for (b = c; 1 < b.length;) {
                    var k = Z.random(b),
                        n = null;
                    d = Infinity;
                    for (c = 0; c < b.length;) {
                        h = b[c];
                        ++c;
                        f = k.point;
                        var p = h.point;
                        p = p.clone();
                        p.normalize(1);
                        f = f.x * p.x + f.y * p.y;
                        d > f && (d = f, n = h)
                    }
                    h = Z.random(a.dcel.vertices.h[a.center.__id__].edges).next.origin;
                    c = Canal.topology.buildPath(n, h);
                    h = null != c ? Canal.topology.buildPath(h, k) : null;
                    if (null != c && null != h)
                        for (d = 0, f = h.length; d < f;) {
                            p = d++;
                            var g = c.indexOf(h[p]);
                            if (-1 != g) {
                                c = a.dcel.vertices2chain(h.slice(0, p).concat(c.slice(g)));
                                if (Canal.validateCourse(c)) return c;
                                break
                            }
                        }
                    hb.trace("discard", {
                        fileName: "Source/com/watabou/mfcg/model/Canal.hx",
                        lineNumber: 216,
                        className: "com.watabou.mfcg.model.Canal",
                        methodName: "regularRiver"
                    });
                    N.remove(b, k);
                    N.remove(b, n)
                }
                return null
            };
            Canal.deltaRiver = function(a) {
                for (var b = [], c = 1, d = a.shoreE.length - 1; c < d;) {
                    var f = c++,
                        h = a.shoreE[f].origin;
                    1 < Z.count(a.cellsByVertex(h), function(a) {
                        return !a.waterbody
                    }) && b.push(f)
                }
                b = Z.sortBy(b, function(b) {
                    return a.shoreE[b].origin.point.get_length()
                });
                f = EdgeChain.vertices(Z.difference(a.earthEdgeE, a.shoreE));
                c = [];
                for (d = 0; d < f.length;) h = f[d], ++d, 1 < a.cellsByVertex(h).length && c.push(h);
                for (f = c; 0 < b.length;) {
                    c = b.shift();
                    d = a.shoreE[c].origin;
                    c = a.shoreE[c + 1].origin.point.subtract(a.shoreE[c - 1].origin.point);
                    c = c.clone();
                    c.normalize(1);
                    var k = new I(-c.y, c.x),
                        n = null,
                        p = -Infinity;
                    for (c = 0; c < f.length;) {
                        h = f[c];
                        ++c;
                        var g = h.point.subtract(d.point);
                        g = g.clone();
                        g.normalize(1);
                        g = k.x * g.x + k.y * g.y;
                        p < g && (p = g, n = h)
                    }
                    c = Canal.topology.buildPath(n, d);
                    if (null != c && (c = a.dcel.vertices2chain(c), Canal.validateCourse(c))) return c;
                    hb.trace("discard", {
                        fileName: "Source/com/watabou/mfcg/model/Canal.hx",
                        lineNumber: 268,
                        className: "com.watabou.mfcg.model.Canal",
                        methodName: "deltaRiver"
                    })
                }
                return null
            };
            Canal.validateCourse = function(a) {
                if (null == a || a.length < Canal.model.earthEdge.length / 5) return !1;
                for (var b = 1, c = a.length - 1; b < c;) {
                    var d = b++;
                    if (null != EdgeChain.edgeByOrigin(Canal.model.shoreE, a[d].origin)) return !1
                }
                if (null !=
                    Canal.model.wall) {
                    var f = Canal.model.wall.edges;
                    b = 0;
                    for (c = f.length; b < c;) {
                        d = b++;
                        var h = f[d];
                        d = EdgeChain.indexByOrigin(a, h.origin);
                        if (0 < d && d < a.length - 1 && !Canal.intersect(f, h, a, a[d])) return !1
                    }
                }
                b = 0;
                for (c = Canal.model.arteries; b < c.length;) {
                    f = c[b];
                    ++b;
                    h = 1;
                    for (var k = f.length - 1; h < k;) {
                        d = h++;
                        var n = f[d];
                        d = EdgeChain.indexByOrigin(a, n.origin);
                        if (0 < d && d < a.length - 1 && !Canal.intersect(f, n, a, a[d])) return !1
                    }
                }
                return !0
            };
            Canal.intersect = function(a, b, c, d) {
                a = EdgeChain.prev(a, b);
                c = EdgeChain.prev(c, d);
                do a = a.next.twin; while (a != c && a != b.twin && a != d.twin);
                if (a == b.twin) return !1;
                do a = a.next.twin; while (a != c && a != b.twin && a != d.twin);
                return a == b.twin ? !0 : !1
            };
            Canal.buildTopology = function(a) {
                if (a.cells != Canal.patches) {
                    Canal.model = a;
                    Canal.patches = a.cells;
                    for (var b = [], c = 0, d = a.cells; c < d.length;) {
                        var f = d[c];
                        ++c;
                        f.waterbody || b.push(f)
                    }
                    Canal.topology = new Topology(b);
                    null != a.wall && Canal.topology.excludePolygon(a.wall.edges);
                    null != a.citadel && Canal.topology.excludePoints(EdgeChain.vertices(va.__cast(a.citadel.ward, Castle).wall.edges));
                    Canal.topology.excludePoints(a.gates);
                    b = 0;
                    for (c = a.arteries; b < c.length;) a = c[b], ++b, Canal.topology.excludePolygon(a)
                }
            };
            Canal.prototype = {
                updateState: function() {
                    this.gates = new pa;
                    if (null != Canal.model.wall)
                        for (var a = 0, b = Canal.model.wall.edges; a < b.length;) {
                            var c = b[a];
                            ++a;
                            var d = c.origin,
                                f = EdgeChain.indexByOrigin(this.course, d);
                            0 < f && f < this.course.length - 1 && (Canal.model.wall.addWatergate(d, this), this.gates.set(d, Canal.model.wall))
                        }
                    this.bridges = new pa;
                    a = 0;
                    for (b = Canal.model.arteries; a < b.length;) {
                        c = b[a];
                        ++a;
                        var h = 0;
                        for (d = c.length; h < d;) {
                            var k = h++,
                                n = c[k];
                            f = EdgeChain.indexByOrigin(this.course, n.origin);
                            0 < f && f < this.course.length - 1 && (0 == k ? this.bridges.set(n.origin,
                                c) : Canal.intersect(c, n, this.course, this.course[f]) && this.bridges.set(n.origin, c))
                        }
                    }
                    n = Canal.model.inner;
                    c = [];
                    a = 2;
                    for (b = this.course.length - 1; a < b;) f = a++, f = this.course[f], -1 == n.indexOf(f.face.data) && -1 == n.indexOf(f.twin.face.data) || Z.add(c, f.origin);
                    a = [];
                    for (b = this.gates.keys(); b.hasNext();) n = b.next(), a.push(n);
                    Z.removeAll(c, a);
                    f = c.length;
                    this.rural = 0 == f;
                    this.width = (3 + Canal.model.inner.length / 5) * (.8 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * .4) * (this.rural ? 1.5 : 1);
                    if (!this.rural) {
                        h = 0;
                        b = n = this.bridges;
                        for (d =
                            n.keys(); d.hasNext();) n = d.next(), b.get(n), -1 != c.indexOf(n) && ++h;
                        a = [];
                        b = n = this.bridges;
                        for (d = n.keys(); d.hasNext();) n = d.next(), b.get(n), a.push(n);
                        for (Z.removeAll(c, a);;) {
                            a = 1 - 2 * h / f;
                            null == a && (a = .5);
                            if (!((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < a)) break;
                            a = [];
                            for (b = 0; b < c.length;) n = c[b], ++b, a.push(1 / n.edges.length);
                            n = Z.weighted(c, a);
                            d = null;
                            this.bridges.set(n, d);
                            N.remove(c, n);
                            ++h
                        }
                    }
                    EdgeChain.assignData(this.course, Tc.CANAL)
                },
                __class__: Canal
            };
            var Cell = function(a) {
                this.seed = -1;
                this.district = null;
                this.face = a;
                this.shape =
                    a.getPoly();
                a.data = this;
                this.waterbody = this.withinWalls = this.withinCity = !1;
                this.seed = C.seed = 48271 * C.seed % 2147483647 | 0
            };
            g["com.watabou.mfcg.model.Cell"] = Cell;
            Cell.__name__ = "com.watabou.mfcg.model.Cell";
            Cell.prototype = {
                bordersInside: function(a) {
                    for (var b = 0; b < a.length;) {
                        var c = a[b];
                        ++b;
                        if (c.face == this.face) return !0
                    }
                    return !1
                },
                isRerollable: function() {
                    return null != this.view ? null != this.view.parent : !1
                },
                reroll: function() {
                    var a = null != this.ward.group ? this.ward.group.core.data : this;
                    a != this ? a.reroll() : this.isRerollable() &&
                        (this.seed = C.seed, this.ward.createGeometry(), this.view.draw(), TownScene.map.updateRoads(), TownScene.map.updateTrees(), ModelDispatcher.geometryChanged.dispatch(this))
                },
                __class__: Cell
            };
            var Tc = y["com.watabou.mfcg.model.Edge"] = {
                __ename__: "com.watabou.mfcg.model.Edge",
                __constructs__: null,
                HORIZON: {
                    _hx_name: "HORIZON",
                    _hx_index: 0,
                    __enum__: "com.watabou.mfcg.model.Edge",
                    toString: r
                },
                COAST: {
                    _hx_name: "COAST",
                    _hx_index: 1,
                    __enum__: "com.watabou.mfcg.model.Edge",
                    toString: r
                },
                ROAD: {
                    _hx_name: "ROAD",
                    _hx_index: 2,
                    __enum__: "com.watabou.mfcg.model.Edge",
                    toString: r
                },
                WALL: {
                    _hx_name: "WALL",
                    _hx_index: 3,
                    __enum__: "com.watabou.mfcg.model.Edge",
                    toString: r
                },
                CANAL: {
                    _hx_name: "CANAL",
                    _hx_index: 4,
                    __enum__: "com.watabou.mfcg.model.Edge",
                    toString: r
                }
            };
            Tc.__constructs__ = [Tc.HORIZON, Tc.COAST, Tc.ROAD, Tc.WALL, Tc.CANAL];
            var City = function(a) {
                this.bounds = new na;
                this.bp = a;
                var b = a.size,
                    c = a.seed;
                if (0 != b) {
                    0 < c && C.reset(c); - 1 == b && (b = City.nextSize);
                    this.nPatches = b;
                    hb.trace(">> seed:" + C.seed + " size:" + b, {
                        fileName: "Source/com/watabou/mfcg/model/City.hx",
                        lineNumber: 124,
                        className: "com.watabou.mfcg.model.City",
                        methodName: "new"
                    });
                    c = City.sizes.h;
                    for (var d = Object.keys(c), f = d.length, h = 0; h < f;) {
                        var k = c[d[h++]];
                        if (b >= k.min && b < k.max) {
                            City.nextSize = k.min + Math.random() * (k.max - k.min) | 0;
                            break
                        }
                    }
                    this.citadelNeeded = a.citadel;
                    this.stadtburgNeeded = a.inner;
                    this.plazaNeeded = a.plaza;
                    this.templeNeeded = a.temple;
                    this.wallsNeeded = a.walls;
                    this.shantyNeeded = a.shanty;
                    this.riverNeeded = a.river;
                    this.coastNeeded = a.coast;
                    this.maxDocks = (Math.sqrt(b / 2) | 0) + (this.riverNeeded ? 2 : 0);
                    do try {
                        hb.trace("--\x3e> seed:" + C.seed + " size:" + b, {
                            fileName: "Source/com/watabou/mfcg/model/City.hx",
                            lineNumber: 151,
                            className: "com.watabou.mfcg.model.City",
                            methodName: "new"
                        }), this.build(), City.instance = this
                    } catch (n) {
                        c = X.caught(n), hb.trace("*** " + H.string(c), {
                            fileName: "Source/com/watabou/mfcg/model/City.hx",
                            lineNumber: 156,
                            className: "com.watabou.mfcg.model.City",
                            methodName: "new"
                        }), City.instance = null
                    }
                    while (null == City.instance);
                    a.updateURL();
                    ModelDispatcher.newModel.dispatch(this)
                }
            };
            g["com.watabou.mfcg.model.City"] = City;
            City.__name__ = "com.watabou.mfcg.model.City";
            City.prototype = {
                rerollName: function() {
                    return Namer.cityName(this)
                },
                setName: function(a, b) {
                    this.bp.name = this.name = a;
                    this.bp.updateURL();
                    ModelDispatcher.titleChanged.dispatch(this.name)
                },
                build: function() {
                    this.streets = [];
                    this.roads = [];
                    this.walls = [];
                    this.landmarks = [];
                    this.north = 0;
                    hb.trace("buildPatches " + id.measure(l(this, this.buildPatches)), {
                        fileName: "Source/com/watabou/mfcg/model/City.hx",
                        lineNumber: 183,
                        className: "com.watabou.mfcg.model.City",
                        methodName: "build"
                    });
                    hb.trace("optimizeJunctions " + id.measure(l(this, this.optimizeJunctions)), {
                        fileName: "Source/com/watabou/mfcg/model/City.hx",
                        lineNumber: 184,
                        className: "com.watabou.mfcg.model.City",
                        methodName: "build"
                    });
                    hb.trace("buildDomains " + id.measure(l(this, this.buildDomains)), {
                        fileName: "Source/com/watabou/mfcg/model/City.hx",
                        lineNumber: 185,
                        className: "com.watabou.mfcg.model.City",
                        methodName: "build"
                    });
                    hb.trace("buildWalls " + id.measure(l(this, this.buildWalls)), {
                        fileName: "Source/com/watabou/mfcg/model/City.hx",
                        lineNumber: 186,
                        className: "com.watabou.mfcg.model.City",
                        methodName: "build"
                    });
                    hb.trace("buildStreets " + id.measure(l(this, this.buildStreets)), {
                        fileName: "Source/com/watabou/mfcg/model/City.hx",
                        lineNumber: 187,
                        className: "com.watabou.mfcg.model.City",
                        methodName: "build"
                    });
                    hb.trace("buildCanals " + id.measure(l(this, this.buildCanals)), {
                        fileName: "Source/com/watabou/mfcg/model/City.hx",
                        lineNumber: 188,
                        className: "com.watabou.mfcg.model.City",
                        methodName: "build"
                    });
                    hb.trace("createWards " + id.measure(l(this, this.createWards)), {
                        fileName: "Source/com/watabou/mfcg/model/City.hx",
                        lineNumber: 189,
                        className: "com.watabou.mfcg.model.City",
                        methodName: "build"
                    });
                    hb.trace("buildCityTowers " +
                        id.measure(l(this, this.buildCityTowers)), {
                            fileName: "Source/com/watabou/mfcg/model/City.hx",
                            lineNumber: 190,
                            className: "com.watabou.mfcg.model.City",
                            methodName: "build"
                        });
                    hb.trace("buildGeometry " + id.measure(l(this, this.buildGeometry)), {
                        fileName: "Source/com/watabou/mfcg/model/City.hx",
                        lineNumber: 191,
                        className: "com.watabou.mfcg.model.City",
                        methodName: "build"
                    });
                    this.updateDimensions()
                },
                buildPatches: function() {
                    for (var a = this, b = 0, c = [new I], d = 2 * Math.PI * ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647), f = 1, h =
                            8 * this.nPatches; f < h;) {
                        var k = f++,
                            n = 10 + k * (2 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647);
                        k = d + 5 * Math.sqrt(k);
                        c.push(I.polar(n, k));
                        b < n && (b = n)
                    }
                    this.plazaNeeded && (C.save(), f = 8 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * 8, h = f * (1 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647), b = Math.max(b, h), c[1] = I.polar(f, d), c[2] = I.polar(h, d + Math.PI / 2), c[3] = I.polar(f, d + Math.PI), c[4] = I.polar(h, d + 3 * Math.PI / 2), C.restore());
                    h = (new Qb(c.concat(PolyCreate.regular(6, 2 * b)))).getVoronoi();
                    for (f = h.keys(); f.hasNext();) c = f.next(), n = h.get(c),
                        Z.some(n, function(a) {
                            return a.get_length() > b
                        }) && h.remove(c);
                    this.cells = [];
                    this.inner = [];
                    f = [];
                    for (n = h.iterator(); n.hasNext();) h = n.next(), f.push(h);
                    this.dcel = new DCEL(f);
                    var p = new pa;
                    f = 0;
                    for (h = this.dcel.faces; f < h.length;) {
                        var g = h[f];
                        ++f;
                        c = new Cell(g);
                        n = PolyCore.centroid(c.shape);
                        p.set(c, n);
                        this.cells.push(c)
                    }
                    this.cells = Z.sortBy(this.cells, function(a) {
                        a = p.h[a.__id__];
                        return a.x * a.x + a.y * a.y
                    });
                    if (this.coastNeeded) {
                        C.save();
                        d = Noise.fractal(6);
                        f = 20 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * 40;
                        k = .3 * b * (((C.seed = 48271 *
                            C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3 * 2 - 1);
                        n = b * (.2 + Math.abs(((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 2 - 1));
                        g = this.bp.coastDir;
                        isNaN(g) && (this.bp.coastDir = Math.floor((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * 20) / 10);
                        C.restore();
                        h = this.bp.coastDir * Math.PI;
                        var q = Math.cos(h),
                            m = Math.sin(h);
                        g = new I(n + f, k);
                        f = 0;
                        for (h = this.cells; f < h.length;) {
                            c = h[f];
                            ++f;
                            var u = p.h[c.__id__],
                                r = new I(u.x * q - u.y * m, u.y * q + u.x * m);
                            u = I.distance(g, r) - n;
                            r.x > g.x && (u = Math.min(u, Math.abs(r.y - k) - n));
                            r = d.get((r.x + b) / (2 * b), (r.y + b) / (2 * b)) * n * Math.sqrt(r.get_length() / b);
                            0 > u + r && (c.waterbody = !0)
                        }
                    }
                    f = n = 0;
                    for (h = this.cells; f < h.length && (c = h[f], ++f, c.waterbody || (c.withinCity = !0, c.withinWalls = this.wallsNeeded, this.inner.push(c), !(++n > this.nPatches))););
                    this.center = Z.min(this.inner[0].shape, function(a) {
                        return a.x * a.x + a.y *
                            a.y
                    });
                    this.plazaNeeded && new Market(this, this.plaza = this.inner[0]);
                    if (this.citadelNeeded) {
                        if (this.stadtburgNeeded) {
                            f = [];
                            h = 0;
                            for (c = this.inner; h < c.length;) n = c[h], ++h,
                                function(b) {
                                    for (var c = b.face.halfEdge, d = c, f = !0; f;) {
                                        var h = d;
                                        d = d.next;
                                        f = d != c;
                                        var k = 0;
                                        for (h = a.cellsByVertex(h.origin); k < h.length;) {
                                            var n = h[k];
                                            ++k;
                                            if (n != b && !n.waterbody && !n.withinCity) return !1
                                        }
                                    }
                                    return !0
                                }(n) && f.push(n);
                            N.remove(f, this.plaza);
                            0 < f.length ? f = Z.random(f) : (hb.trace("Unable to build an uraban castle!", {
                                fileName: "Source/com/watabou/mfcg/model/City.hx",
                                lineNumber: 332,
                                className: "com.watabou.mfcg.model.City",
                                methodName: "buildPatches"
                            }), k = this.inner, f = this.citadel = k[k.length - 1]);
                            this.citadel = f
                        } else k = this.inner, this.citadel = k[k.length - 1];
                        this.citadel.withinCity = !0;
                        this.citadel.withinWalls = !0;
                        N.remove(this.inner, this.citadel)
                    }
                },
                optimizeJunctions: function() {
                    var a = 3 * CurtainWall.LTOWER_RADIUS,
                        b = this.citadel;
                    for (b = null != b ? b.shape : null;;) {
                        for (var c = !1, d = 0, f = this.dcel.faces; d < f.length;) {
                            var h = f[d];
                            ++d;
                            var k = h.data.shape;
                            if (!(4 >= k.length)) {
                                var n = PolyCore.perimeter(k);
                                k =
                                    Math.max(a, n / k.length / 3);
                                n = h = h.halfEdge;
                                for (var p = !0; p;) {
                                    var g = n;
                                    n = n.next;
                                    p = n != h;
                                    if (!(null == g.twin || 4 >= g.twin.face.data.shape.length || null != b && -1 != b.indexOf(g.origin.point) != (-1 != b.indexOf(g.next.origin.point))) && I.distance(g.origin.point, g.next.origin.point) < k) {
                                        c = 0;
                                        for (k = this.dcel.collapseEdge(g).edges; c < k.length;) h = k[c], ++c, h.face.data.shape = h.face.getPoly();
                                        c = !0;
                                        break
                                    }
                                }
                            }
                        }
                        if (!c) break
                    }
                    null == this.dcel.vertices.h[this.center.__id__] && (this.center = Z.min(this.inner[0].shape, function(a) {
                        return a.x * a.x +
                            a.y * a.y
                    }))
                },
                buildWalls: function() {
                    C.save();
                    var a = this.waterEdge;
                    null != this.citadel && (a = a.concat(this.citadel.shape));
                    this.border = new CurtainWall(this.wallsNeeded, this, this.inner, a);
                    this.wallsNeeded && (this.wall = this.border, this.walls.push(this.wall));
                    this.gates = this.border.gates;
                    null != this.citadel && (a = new Castle(this, this.citadel), a.wall.buildTowers(), this.walls.push(a.wall), this.gates = this.gates.concat(a.wall.gates));
                    C.restore()
                },
                cellsByVertex: function(a) {
                    var b = [],
                        c = 0;
                    for (a = a.edges; c < a.length;) {
                        var d = a[c];
                        ++c;
                        b.push(d.face.data)
                    }
                    return b
                },
                buildDomains: function() {
                    var a = Z.find(this.dcel.edges, function(a) {
                        return null == a.twin
                    });
                    this.horizonE = DCEL.circumference(a, this.dcel.faces);
                    if (6 > this.horizonE.length) throw X.thrown("Failed to build the horizon: " + this.horizonE.length);
                    EdgeChain.assignData(this.horizonE, Tc.HORIZON);
                    this.horizon = EdgeChain.toPoly(this.horizonE);
                    if (this.coastNeeded) {
                        a = [];
                        for (var b = [], c = 0, d = this.dcel.faces; c < d.length;) {
                            var f = d[c];
                            ++c;
                            f.data.waterbody ? a.push(f) : b.push(f)
                        }
                        b = Z.max(DCEL.split(b), function(a) {
                            return a.length
                        });
                        a = Z.max(DCEL.split(a), function(a) {
                            return a.length
                        });
                        this.earthEdgeE = DCEL.circumference(null, b);
                        this.earthEdge = EdgeChain.toPoly(this.earthEdgeE);
                        this.waterEdgeE = DCEL.circumference(null, a);
                        if (Z.every(this.waterEdgeE, function(a) {
                                return null != a.twin
                            })) throw X.thrown("Required water doesn't touch the horizon");
                        this.waterEdge = EdgeChain.toPoly(this.waterEdgeE);
                        PolyCore.set(this.waterEdge, PolyUtils.smooth(this.waterEdge, null, Math.floor(1 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * 3)));
                        for (a = 0; null != this.earthEdgeE[a].twin;) a = (a + 1) % this.earthEdgeE.length;
                        for (; null == this.earthEdgeE[a].twin;) a = (a + 1) % this.earthEdgeE.length;
                        this.shore = [];
                        this.shoreE = [];
                        do b = this.earthEdgeE[a], this.shoreE.push(b), this.shore.push(b.origin.point), a = (a + 1) % this.earthEdgeE.length; while (null != this.earthEdgeE[a].twin);
                        EdgeChain.assignData(this.shoreE, Tc.COAST)
                    } else this.earthEdgeE = this.horizonE, this.earthEdge = this.horizon, this.waterEdgeE = [], this.waterEdge = [], this.shoreE = [], this.shore = []
                },
                buildStreets: function() {
                    for (var a = [], b = 0, c = this.cells; b < c.length;) {
                        var d = c[b];
                        ++b;
                        d.withinCity &&
                            a.push(d)
                    }
                    var f = new Topology(a);
                    a = [];
                    b = 0;
                    for (c = this.cells; b < c.length;) d = c[b], ++b, d.withinCity || d.waterbody || a.push(d);
                    d = new Topology(a);
                    a = [];
                    b = 0;
                    for (c = this.shoreE; b < c.length;) {
                        var h = c[b];
                        ++b;
                        a.push(h.origin)
                    }
                    f.excludePoints(a);
                    d.excludePoints(a);
                    h = [];
                    a = 0;
                    for (b = this.walls; a < b.length;) {
                        var k = b[a];
                        ++a;
                        Z.addAll(h, EdgeChain.vertices(k.edges))
                    }
                    0 < h.length && (Z.removeAll(h, this.gates), f.excludePoints(h), d.excludePoints(h));
                    h = Z.difference(this.earthEdgeE, this.shoreE);
                    a = [];
                    b = 0;
                    for (c = h; b < c.length;) {
                        var n = c[b];
                        ++b;
                        null != d.pt2node.h.__keys__[n.origin.__id__] &&
                            a.push(n)
                    }
                    h = a;
                    a = 0;
                    for (b = this.gates; a < b.length;)
                        if (k = [b[a]], ++a, c = null != this.plaza ? Z.min(this.plaza.shape, function(a) {
                                return function(b) {
                                    var c = a[0].point,
                                        d = b.x - c.x;
                                    b = b.y - c.y;
                                    return d * d + b * b
                                }
                            }(k)) : this.center, c = f.buildPath(k[0], this.dcel.vertices.h[c.__id__]), null != c) {
                            if (c = this.dcel.vertices2chain(c), this.streets.push(c), -1 != this.border.gates.indexOf(k[0])) {
                                n = null;
                                if (null != d.pt2node.h.__keys__[k[0].__id__])
                                    for (h = Z.sortBy(h, function(a) {
                                            return function(b) {
                                                b = b.origin.point;
                                                var c = a[0].point;
                                                return -(c.x *
                                                    b.x + c.y * b.y) / b.get_length()
                                            }
                                        }(k)), c = 0; c < h.length && (n = h[c], ++c, n = d.buildPath(n.origin, k[0]), null == n););
                                c = n;
                                if (null != c) k = this.dcel.vertices2chain(c), d.excludePolygon(k), this.roads.push(k);
                                else if (null != this.wall) {
                                    c = [];
                                    n = 0;
                                    for (k = this.cellsByVertex(k[0]); n < k.length;) {
                                        var p = k[n];
                                        ++n;
                                        !p.withinWalls && p.bordersInside(this.shoreE) && c.push(p)
                                    }
                                    k = c;
                                    for (c = 0; c < k.length;) n = k[c], ++c, n.landing = !0, n.withinCity = !0, new Alleys(this, n), this.maxDocks--
                                }
                            }
                        } else hb.trace("Unable to build a street!", {
                            fileName: "Source/com/watabou/mfcg/model/City.hx",
                            lineNumber: 591,
                            className: "com.watabou.mfcg.model.City",
                            methodName: "buildStreets"
                        });
                    this.tidyUpRoads();
                    if (this.wallsNeeded) {
                        a = [];
                        b = 0;
                        for (c = this.gates; b < c.length;) f = c[b], ++b, a.push(f.point);
                        f = a
                    } else f = null;
                    a = 0;
                    for (b = this.arteries; a < b.length;) d = b[a], ++a, EdgeChain.assignData(d, Tc.ROAD), d = EdgeChain.toPoly(d), PolyCore.set(d, PolyUtils.smoothOpen(d, f, 2))
                },
                tidyUpRoads: function() {
                    for (var a = [], b = 0, c = this.streets; b < c.length;) {
                        var d = c[b];
                        ++b;
                        Z.addAll(a, d)
                    }
                    b = 0;
                    for (c = this.roads; b < c.length;) d = c[b], ++b, Z.addAll(a, d);
                    for (this.arteries = []; 0 <
                        a.length;) {
                        d = a.pop();
                        var f = !1;
                        b = 0;
                        for (c = this.arteries; b < c.length;) {
                            var h = c[b];
                            ++b;
                            if (h[0].origin == d.next.origin) {
                                h.unshift(d);
                                f = !0;
                                break
                            } else if (h[h.length - 1].next.origin == d.origin) {
                                h.push(d);
                                f = !0;
                                break
                            }
                        }
                        f || this.arteries.push([d])
                    }
                },
                buildCanals: function() {
                    C.save();
                    this.canals = this.riverNeeded ? [Canal.createRiver(this)] : [];
                    C.restore()
                },
                addHarbour: function(a) {
                    for (var b = 0, c = [], d = a = a.face.halfEdge, f = !0; f;) {
                        var h = d;
                        d = d.next;
                        f = d != a;
                        null != h.twin && c.push(h.twin.face.data)
                    }
                    for (; b < c.length;) a = c[b], ++b, a.waterbody &&
                        null == a.ward && new Harbour(this, a)
                },
                createWards: function() {
                    if (this.bp.greens) {
                        var a = 0;
                        if (null != this.citadel) {
                            var b = this.cellsByVertex(this.citadel.ward.wall.gates[0]);
                            if (3 == b.length) {
                                var c = 1 - 2 / (this.nPatches - 1);
                                null == c && (c = .5);
                                var d = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < c
                            } else d = !1;
                            if (d)
                                for (d = 0; d < b.length;) {
                                    var f = b[d];
                                    ++d;
                                    null == f.ward && (new Park(this, f), ++a)
                                }
                        }
                        d = (this.nPatches - 10) / 20;
                        c = d - (d | 0);
                        null == c && (c = .5);
                        a = (d | 0) + ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < c ? 1 : 0) - a;
                        for (d = 0; d < a;)
                            for (d++;;)
                                if (f =
                                    Z.random(this.inner), null == f.ward) {
                                    new Park(this, f);
                                    break
                                }
                    }
                    if (0 < this.shoreE.length && 0 < this.maxDocks)
                        for (d = 0, a = this.inner; d < a.length && !(f = a[d], ++d, f.bordersInside(this.shoreE) && (f.landing = !0, 0 >= --this.maxDocks)););
                    this.templeNeeded && (f = Z.min(this.inner, function(a) {
                        return null == a.ward ? PolyCore.center(a.shape).get_length() : Infinity
                    }), new Cathedral(this, f));
                    d = 0;
                    for (a = this.inner; d < a.length;) f = a[d], ++d, null == f.ward && new Alleys(this, f);
                    if (null != this.wall)
                        for (d = 0, a = this.wall.gates; d < a.length;)
                            if (b = a[d], ++d, c = 1 / (this.nPatches -
                                    5), null == c && (c = .5), !((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < c))
                                for (c = 0, b = this.cellsByVertex(b); c < b.length;) f = b[c], ++c, null == f.ward && (f.withinCity = !0, f.bordersInside(this.shoreE) && 0 < this.maxDocks-- && (f.landing = !0), new Alleys(this, f));
                    this.shantyNeeded && this.buildShantyTowns();
                    d = 0;
                    for (a = EdgeChain.vertices(this.shoreE); d < a.length;)
                        for (f = a[d], ++d, c = 0, b = this.cellsByVertex(f); c < b.length;) {
                            var h = b[c];
                            ++c;
                            if (h.withinCity && !h.landing) {
                                for (var k = h.face.halfEdge; k.next.origin != f;) k = k.next;
                                if (k.twin.face.data.landing &&
                                    k.next.twin.face.data.landing) {
                                    h.landing = !0;
                                    break
                                }
                            }
                        }
                    d = 0;
                    for (a = this.cells; d < a.length;) f = a[d], ++d, f.landing && this.addHarbour(f);
                    this.buildFarms()
                },
                updateDimensions: function() {
                    for (var a = 0, b = 0, c = 0, d = 0, f = function(f) {
                            for (var h = 0; h < f.length;) {
                                var k = f[h];
                                ++h;
                                k.x < a ? a = k.x : k.x > b && (b = k.x);
                                k.y < c ? c = k.y : k.y > d && (d = k.y)
                            }
                        }, h = 0, k = this.districts; h < k.length;) {
                        var n = k[h];
                        ++h;
                        var p = 0;
                        for (n = n.groups; p < n.length;) {
                            var g = n[p];
                            ++p;
                            var q = 0;
                            for (g = g.blocks; q < g.length;) {
                                var m = g[q];
                                ++q;
                                f(m.shape)
                            }
                        }
                    }
                    null != this.citadel && f(va.__cast(this.citadel.ward,
                        Castle).wall.shape);
                    this.bounds.setTo(a, c, b - a, d - c)
                },
                getViewport: function() {
                    return null != this.focus ? this.focus.getBounds() : this.bounds
                },
                buildFarms: function() {
                    for (var a = ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3 * 2, b = ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3, c = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * Math.PI *
                            2, d = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * Math.PI * 2, f = 0, h = 0, k = this.inner; h < k.length;) {
                        var n = k[h];
                        ++h;
                        var p = 0;
                        for (n = n.shape; p < n.length;) {
                            var g = n[p];
                            ++p;
                            f = Math.max(f, I.distance(g, this.center))
                        }
                    }
                    h = 0;
                    for (k = this.cells; h < k.length;) n = k[h], ++h, null == n.ward && (n.waterbody ? new Ward(this, n) : n.bordersInside(this.shoreE) ? new Wilderness(this, n) : (p = PolyCore.center(n.shape).subtract(this.center), g = Math.atan2(p.y, p.x), g = a * Math.sin(g + c) + b * Math.sin(2 * g + d), p.get_length() < (g + 1) * f ? new Farm(this, n) : new Wilderness(this, n)))
                },
                buildShantyTowns: function() {
                    for (var a =
                            this, b = [], c = [], d = function(b) {
                                for (var c = 3 * I.distance(b, a.center), d = 0, f = a.roads; d < f.length;) {
                                    var h = f[d];
                                    ++d;
                                    for (var k = 0; k < h.length;) {
                                        var n = h[k];
                                        ++k;
                                        c = Math.min(c, 2 * I.distance(n.origin.point, b))
                                    }
                                }
                                d = 0;
                                for (f = a.shoreE; d < f.length;) k = f[d], ++d, c = Math.min(c, I.distance(k.origin.point, b));
                                d = 0;
                                for (f = a.canals; d < f.length;)
                                    for (n = f[d], ++d, k = 0, h = n.course; k < h.length;) n = h[k], ++k, c = Math.min(c, I.distance(n.origin.point, b));
                                return c * c
                            }, f = function(f) {
                                for (var h = 0, k = [], n = f.face.halfEdge, p = n, g = !0; g;) {
                                    var q = p;
                                    p = p.next;
                                    g = p != n;
                                    null != q.twin && k.push(q.twin.face.data)
                                }
                                for (f = k; h < f.length;) {
                                    var P = f[h];
                                    ++h;
                                    if (!P.withinCity && !P.waterbody && !P.bordersInside(a.horizonE)) {
                                        k = [];
                                        p = n = P.face.halfEdge;
                                        for (g = !0; g;) q = p, p = p.next, g = p != n, null != q.twin && k.push(q.twin.face.data);
                                        k = Z.count(k, function(a) {
                                            return a.withinCity
                                        });
                                        1 < k && Z.add(b, P) && c.push(k * k / d(PolyCore.center(P.shape)))
                                    }
                                }
                            }, h = 0, k = this.cells; h < k.length;) {
                        var n = k[h];
                        ++h;
                        n.withinCity && f(n)
                    }
                    var p = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647;
                    for (p = this.nPatches * (1 + p * p * p) * .5; 0 < p && 0 < b.length;) {
                        h = [];
                        k = 0;
                        for (n = c.length; k < n;) {
                            var g = k++;
                            h.push(g)
                        }
                        h = Z.weighted(h, c);
                        n = b[h];
                        n.withinCity = !0;
                        0 < this.maxDocks && n.bordersInside(this.shoreE) && (n.landing = !0, this.maxDocks--);
                        new Alleys(this, n);
                        c.splice(h, 1);
                        N.remove(b, n);
                        --p;
                        f(n)
                    }
                },
                buildCityTowers: function() {
                    if (null != this.wall) {
                        for (var a = 0, b = this.wall.edges.length; a < b;) {
                            var c = a++,
                                d = this.wall.edges[c];
                            if (d.data == Tc.COAST || d.twin.face.data == this.citadel) this.wall.segments[c] = !1
                        }
                        this.wall.buildTowers();
                        if (null != this.citadel)
                            for (a = 0, b = va.__cast(this.citadel.ward,
                                    Castle).wall.towers; a < b.length;) c = b[a], ++a, N.remove(this.wall.towers, c)
                    }
                },
                getOcean: function() {
                    if (null != this.ocean) return this.ocean;
                    for (var a = [], b = !1, c = 0, d = this.waterEdgeE; c < d.length;) {
                        var f = d[c];
                        ++c;
                        var h = f.origin.point;
                        if (null == f.twin) a.push(h);
                        else {
                            f = f.twin.face.data;
                            var k = !1,
                                n = !1;
                            f.landing ? k = !0 : f.withinCity && PolyAccess.isConvexVertexi(this.earthEdge, this.earthEdge.indexOf(h)) && (n = !0);
                            if (b || k) n = !0;
                            b = k;
                            n && a.push(h)
                        }
                    }
                    return this.ocean = Chaikin.render(this.waterEdge, !0, 3, a)
                },
                buildGeometry: function() {
                    for (var a = 0, b =
                            this.canals; a < b.length;) {
                        var c = b[a];
                        ++a;
                        c.updateState()
                    }
                    Namer.reset();
                    this.name = null != this.bp.name ? this.bp.name : this.rerollName();
                    a = new DistrictBuilder(this);
                    a.build();
                    this.districts = a.districts;
                    Namer.nameDistricts(this);
                    a = 0;
                    for (b = this.cells; a < b.length;) c = b[a], ++a, c.ward.createGeometry()
                },
                rerollDistricts: function() {
                    Namer.reset();
                    Namer.nameDistricts(this);
                    ModelDispatcher.districtsChanged.dispatch()
                },
                updateGeometry: function(a) {
                    for (var b = [], c = [], d = 0; d < a.length;) {
                        var f = a[d];
                        ++d;
                        f.ward instanceof Alleys ? Z.add(c, f.ward.group.core) : f.ward.createGeometry();
                        null != f.district && Z.add(b, f.district)
                    }
                    for (d = 0; d < c.length;) f = c[d], ++d, f.data.ward.createGeometry();
                    for (d = 0; d < b.length;) c = b[d], ++d, c.updateGeometry();
                    TreesLayer.resetForests();
                    ModelDispatcher.geometryChanged.dispatch(1 == a.length ? a[0] : null)
                },
                updateLots: function() {
                    for (var a = 0, b = this.cells; a < b.length;) {
                        var c = b[a];
                        ++a;
                        c.ward instanceof Alleys && c.ward.createGeometry()
                    }
                    ModelDispatcher.geometryChanged.dispatch(null)
                },
                addLandmark: function(a) {
                    a = new Landmark(this, a);
                    this.landmarks.push(a);
                    ModelDispatcher.landmarksChanged.dispatch();
                    return a
                },
                updateLandmarks: function() {
                    for (var a =
                            0, b = this.landmarks; a < b.length;) {
                        var c = b[a];
                        ++a;
                        c.update()
                    }
                },
                addLandmarks: function(a) {
                    for (var b = [], c = 0, d = this.districts; c < d.length;) {
                        var f = d[c];
                        ++c;
                        var h = 0;
                        for (f = f.groups; h < f.length;) {
                            var k = f[h];
                            ++h;
                            var n = 0;
                            for (k = k.blocks; n < k.length;) {
                                var p = k[n];
                                ++n;
                                p = p.lots;
                                for (var g = 0; g < p.length;) {
                                    var q = p[g];
                                    ++g;
                                    b.push(q)
                                }
                            }
                        }
                    }
                    for (c = 0; c < a.length;) h = a[c], ++c, d = Z.random(b), f = PolyCore.center(d), h = new Landmark(this, f, h), this.landmarks.push(h), N.remove(b, d);
                    ModelDispatcher.landmarksChanged.dispatch()
                },
                removeLandmark: function(a) {
                    N.remove(this.landmarks,
                        a);
                    ModelDispatcher.landmarksChanged.dispatch()
                },
                removeLandmarks: function() {
                    this.landmarks = [];
                    ModelDispatcher.landmarksChanged.dispatch()
                },
                countBuildings: function() {
                    for (var a = 0, b = 0, c = this.districts; b < c.length;) {
                        var d = c[b];
                        ++b;
                        var f = 0;
                        for (d = d.groups; f < d.length;) {
                            var h = d[f];
                            ++f;
                            var k = 0;
                            for (h = h.blocks; k < h.length;) {
                                var n = h[k];
                                ++k;
                                a += n.lots.length
                            }
                        }
                    }
                    return a
                },
                getNeighbour: function(a, b) {
                    for (var c = a = a.face.halfEdge, d = !0; d;) {
                        var f = c;
                        c = c.next;
                        d = c != a;
                        if (f.origin == b) {
                            b = f.twin;
                            b = null != b ? b.face : null;
                            if (null != b) return b.data;
                            break
                        }
                    }
                    return null
                },
                getCell: function(a) {
                    for (var b = 0, c = this.cells; b < c.length;) {
                        var d = c[b];
                        ++b;
                        if (PolyBounds.containsPoint(d.shape, a)) return d
                    }
                    return null
                },
                getDetails: function(a) {
                    for (var b = 0, c = 0, d = this.districts; c < d.length;) {
                        var f = d[c];
                        ++c;
                        var h = 0;
                        for (f = f.groups; h < f.length;) {
                            var k = f[h];
                            ++h;
                            if (null == this.focus || Z.intersects(k.faces, this.focus.faces)) {
                                var n = 0;
                                for (k = k.blocks; n < k.length;) {
                                    var p = k[n];
                                    ++n;
                                    PolyBounds.rect(p.shape).intersects(a) && ++b
                                }
                            }
                        }
                    }
                    c = 0;
                    for (d = this.walls; c < d.length;)
                        for (f = d[c], ++c, h = 0, f = f.towers; h < f.length;) n = f[h], ++h, (null ==
                            this.focus || -1 != this.focus.vertices.indexOf(n)) && a.containsPoint(n.point) && ++b;
                    return b
                },
                splitEdge: function(a) {
                    var b = this.dcel.splitEdge(a);
                    a.face.data.shape = a.face.getPoly();
                    a.twin.face.data.shape = a.twin.face.getPoly();
                    return b
                },
                getCanalWidth: function(a) {
                    return this.canals[0].width
                },
                getFMGParams: function() {
                    return {
                        size: this.nPatches,
                        seed: this.bp.seed,
                        name: this.name,
                        coast: 0 < this.shoreE.length ? 1 : 0,
                        port: Z.some(this.cells, function(a) {
                            return a.landing
                        }) ? 1 : 0,
                        river: 0 < this.canals.length ? 1 : 0,
                        sea: 0 < this.shoreE.length ?
                            this.bp.coastDir : 0
                    }
                },
                __class__: City
            };
            var CurtainWall = function(a, b, c, d) {
                this.watergates = new pa;
                this.real = !0;
                this.patches = c;
                if (1 == c.length) {
                    for (var f = [], h = c[0].face.halfEdge, k = h, n = !0; n;) {
                        var p = k;
                        k = k.next;
                        n = k != h;
                        f.push(p)
                    }
                    this.edges = f;
                    this.shape = c[0].shape
                } else {
                    f = [];
                    for (h = 0; h < c.length;) k = c[h], ++h, f.push(k.face);
                    this.edges = DCEL.circumference(null, f);
                    this.shape = EdgeChain.toPoly(this.edges)
                }
                a && (EdgeChain.assignData(this.edges, Tc.WALL, !1), 1 < c.length && PolyCore.set(this.shape, PolyUtils.smooth(this.shape, d, 3)));
                this.length = this.shape.length;
                1 == c.length ?
                    this.buildCastleGate(b, d) : this.buildCityGates(a, b, d);
                f = [];
                h = 0;
                for (a = this.shape; h < a.length;) ++h, f.push(!0);
                this.segments = f
            };
            g["com.watabou.mfcg.model.CurtainWall"] = CurtainWall;
            CurtainWall.__name__ = "com.watabou.mfcg.model.CurtainWall";
            CurtainWall.prototype = {
                buildCityGates: function(a, b, c) {
                    this.gates = [];
                    for (var d = [], f = 0, h = this.edges; f < h.length;) {
                        var k = h[f];
                        ++f;
                        d.push(-1 != c.indexOf(k.origin.point) || 2 > Z.intersect(b.cellsByVertex(k.origin), this.patches).length ? 0 : 1)
                    }
                    var n = d;
                    if (0 == Z.sum(n)) throw hb.trace("" + this.length + " vertices ScaleBarOld " +
                        this.patches.length + " patches, " + c.length + " are reserved.", {
                            fileName: "Source/com/watabou/mfcg/model/CurtainWall.hx",
                            lineNumber: 82,
                            className: "com.watabou.mfcg.model.CurtainWall",
                            methodName: "buildCityGates"
                        }), new Vb("No valid vertices to create gates!");
                    for (var p = -1 < b.bp.gates ? b.bp.gates : b.bp.hub ? this.shape.length : 2 + (this.patches.length / 12 * (0 < b.shoreE.length ? .75 : 1) | 0); this.gates.length < p && 0 < Z.sum(n);) {
                        d = [];
                        f = 0;
                        for (h = n.length; f < h;) k = f++, d.push(k);
                        d = Z.weighted(d, n);
                        f = [this.edges[d].origin];
                        this.gates.push(f[0]);
                        if (a && (k = Z.difference(b.cellsByVertex(f[0]), this.patches), 1 == k.length)) {
                            k = k[0];
                            h = Z.difference(k.shape, c);
                            var g = this.shape,
                                q = g.indexOf(f[0].point);
                            if (-1 != q) {
                                var m = g.length;
                                g = g[(q + m - 1) % m]
                            } else g = null;
                            q = this.shape;
                            m = q.indexOf(f[0].point);
                            q = -1 != m ? q[(m + 1) % q.length] : null;
                            Z.removeAll(h, this.shape);
                            if (0 < h.length) {
                                g = [f[0].point.subtract(GeomUtils.lerp(g, q))];
                                h = Z.max(h, function(a, b) {
                                    return function(c) {
                                        c = c.subtract(b[0].point);
                                        return (c.x * a[0].x + c.y * a[0].y) / c.get_length()
                                    }
                                }(g, f));
                                f = b.dcel.splitFace(k.face, f[0], b.dcel.vertices.h[h.__id__]);
                                f = [f.face, f.twin.face];
                                h = b.cells;
                                g = [];
                                for (q = 0; q < f.length;) m = f[q], ++q, g.push(new Cell(m));
                                Z.replace(h, k, g);
                                for (h = 0; h < f.length;)
                                    for (k = f[h], ++h, q = g = k.halfEdge, m = !0; m;) k = q, q = q.next, m = q != g, null != k.twin && k.twin.data == Tc.WALL && (k.data = Tc.WALL)
                            }
                        }
                        k = 0;
                        for (f = n.length; k < f;) h = k++, g = Math.abs(h - d), g > n.length / 2 && (g = n.length - g), n[h] *= 1 >= g ? 0 : g - 1
                    }
                    if (0 == this.gates.length && 0 < p) throw new Vb("No gates created!");
                    if (a)
                        for (d = 0, f = this.gates; d < f.length;) a = f[d], ++d, PointExtender.set(a.point, PolyUtils.lerpVertex(this.shape, a.point))
                },
                buildCastleGate: function(a,
                    b) {
                    for (var c = 0, d = this.edges; c < d.length;) {
                        var f = d[c];
                        ++c;
                        if (f.twin.face.data == a.plaza) {
                            this.gates = [this.splitSegment(a, f)];
                            return
                        }
                    }
                    c = Z.difference(this.shape, b);
                    if (0 == c.length) {
                        c = [];
                        d = 0;
                        for (b = this.edges; d < b.length;) f = b[d], ++d, f.twin.face.data.withinCity && c.push(f);
                        0 == c.length ? (hb.trace("No suitable edge to split", {
                            fileName: "Source/com/watabou/mfcg/model/CurtainWall.hx",
                            lineNumber: 169,
                            className: "com.watabou.mfcg.model.CurtainWall",
                            methodName: "buildCastleGate"
                        }), this.gates = [Z.min(this.edges, function(a) {
                            return I.distance(a.origin.point,
                                a.next.origin.point)
                        }).origin]) : (f = Z.min(c, function(a) {
                            return GeomUtils.lerp(a.origin.point, a.next.origin.point, .5).get_length()
                        }), this.gates = [this.splitSegment(a, f)])
                    } else c = Z.min(c, function(a) {
                        return a.get_length()
                    }), PointExtender.set(c, PolyUtils.lerpVertex(this.shape, c)), this.gates = [a.dcel.vertices.h[c.__id__]]
                },
                splitSegment: function(a, b) {
                    a = a.splitEdge(b);
                    for (var c = [], d = this.patches[0].face.halfEdge, f = d, h = !0; h;) b = f, f = f.next, h = f != d, c.push(b);
                    this.edges = c;
                    this.shape = this.patches[0].shape;
                    this.length++;
                    EdgeChain.assignData(this.edges,
                        Tc.WALL, !1);
                    return a.origin
                },
                buildTowers: function() {
                    this.towers = [];
                    this.coastTowers = [];
                    if (this.real)
                        for (var a = 0, b = this.length; a < b;) {
                            var c = a++,
                                d = (c + this.length - 1) % this.length,
                                f = this.edges[c].origin;
                            if (-1 == this.gates.indexOf(f) && (this.segments[d] || this.segments[c])) {
                                this.towers.push(f);
                                for (var h = null, k = null, n = 0, p = f.edges; n < p.length;) {
                                    var g = p[n];
                                    ++n;
                                    g.data == Tc.COAST && (null == h ? h = g : k = g)
                                }
                                null != k && (h = h.face.data.waterbody ? [k.next.origin, h.next.origin] : [h.next.origin, k.next.origin], h.push(f), h.push(this.edges[this.segments[c] ?
                                    (c + 1) % this.length : d].origin), this.coastTowers.push(h))
                            }
                        }
                },
                bothSegments: function(a) {
                    return this.segments[a] ? this.segments[(a + this.length - 1) % this.length] : !1
                },
                addWatergate: function(a, b) {
                    this.watergates.set(a, b);
                    N.remove(this.towers, a)
                },
                getTowerRadius: function(a) {
                    return this.real ? -1 != this.towers.indexOf(a) ? CurtainWall.LTOWER_RADIUS : -1 != this.gates.indexOf(a) ? 1 + 2 * CurtainWall.TOWER_RADIUS : 0 : 0
                },
                __class__: CurtainWall
            };
            var lc = y["com.watabou.mfcg.model.DistrictType"] = {
                __ename__: "com.watabou.mfcg.model.DistrictType",
                __constructs__: null,
                CENTER: (G = function(a) {
                    return {
                        _hx_index: 0,
                        plaza: a,
                        __enum__: "com.watabou.mfcg.model.DistrictType",
                        toString: r
                    }
                }, G._hx_name = "CENTER", G.__params__ = ["plaza"], G),
                CASTLE: (G = function(a) {
                    return {
                        _hx_index: 1,
                        castle: a,
                        __enum__: "com.watabou.mfcg.model.DistrictType",
                        toString: r
                    }
                }, G._hx_name = "CASTLE", G.__params__ = ["castle"], G),
                DOCKS: {
                    _hx_name: "DOCKS",
                    _hx_index: 2,
                    __enum__: "com.watabou.mfcg.model.DistrictType",
                    toString: r
                },
                BRIDGE: (G = function(a) {
                    return {
                        _hx_index: 3,
                        bridge: a,
                        __enum__: "com.watabou.mfcg.model.DistrictType",
                        toString: r
                    }
                }, G._hx_name = "BRIDGE", G.__params__ = ["bridge"], G),
                GATE: (G = function(a) {
                    return {
                        _hx_index: 4,
                        gate: a,
                        __enum__: "com.watabou.mfcg.model.DistrictType",
                        toString: r
                    }
                }, G._hx_name = "GATE", G.__params__ = ["gate"], G),
                BANK: {
                    _hx_name: "BANK",
                    _hx_index: 5,
                    __enum__: "com.watabou.mfcg.model.DistrictType",
                    toString: r
                },
                PARK: {
                    _hx_name: "PARK",
                    _hx_index: 6,
                    __enum__: "com.watabou.mfcg.model.DistrictType",
                    toString: r
                },
                SPRAWL: {
                    _hx_name: "SPRAWL",
                    _hx_index: 7,
                    __enum__: "com.watabou.mfcg.model.DistrictType",
                    toString: r
                },
                REGULAR: {
                    _hx_name: "REGULAR",
                    _hx_index: 8,
                    __enum__: "com.watabou.mfcg.model.DistrictType",
                    toString: r
                }
            };
            lc.__constructs__ = [lc.CENTER, lc.CASTLE, lc.DOCKS, lc.BRIDGE, lc.GATE, lc.BANK, lc.PARK, lc.SPRAWL, lc.REGULAR];
            var District = function(a, b) {
                this.type = b;
                this.city = a[0].ward.model;
                b = [];
                for (var c = 0; c < a.length;) {
                    var d = a[c];
                    ++c;
                    b.push(d.face)
                }
                this.faces = b;
                for (b = 0; b < a.length;) c = a[b], ++b, c.district = this;
                this.createParams()
            };
            g["com.watabou.mfcg.model.District"] = District;
            District.__name__ = "com.watabou.mfcg.model.District";
            District.check4holes = function(a) {
                for (var b = [],
                        c = [], d = 0; d < a.length;) {
                    var f = a[d];
                    ++d;
                    for (var h = f.halfEdge, k = h, n = !0; n;)
                        if (f = k, k = k.next, n = k != h, null == f.twin || -1 == a.indexOf(f.twin.face)) {
                            if (-1 != c.indexOf(f.origin)) return !0;
                            b.push(f);
                            c.push(f.origin)
                        }
                }
                a = 0;
                f = c = b[0];
                do
                    for (++a, f = f.next; - 1 == b.indexOf(f);) f = f.twin.next; while (f != c);
                return b.length > a
            };
            District.updateColors = function(a) {
                var b = a.length;
                if (1 == b) a[0].color = K.colorRoof;
                else
                    for (var c = 0; c < b;) {
                        var d = c++;
                        a[d].color = K.getTint(K.colorRoof, d, b)
                    }
            };
            District.prototype = {
                createParams: function() {
                    this.alleys = {
                        minSq: 15 +
                            40 * Math.abs(((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 2 - 1),
                        gridChaos: .2 + ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3 * .8,
                        sizeChaos: .4 + ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) /
                            2147483647) / 3 * .6,
                        shapeFactor: .25 + ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3 * 2,
                        inset: .6 * (1 - Math.abs(((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 2 - 1)),
                        blockSize: 4 + 10 * (((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed =
                            48271 * C.seed % 2147483647 | 0) / 2147483647) / 3)
                    };
                    this.alleys.minFront = Math.sqrt(this.alleys.minSq);
                    this.greenery = Math.pow(((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3, this.type == lc.PARK ? 1 : 2);
                    this.type == lc.SPRAWL && (this.alleys.gridChaos *= .5, this.alleys.blockSize *= 2, this.greenery = (1 + this.greenery) / 2)
                },
                updateGeometry: function() {
                    if (1 == this.faces.length) {
                        var a = [];
                        for (var b = this.faces[0].halfEdge, c = b, d = !0; d;) {
                            var f =
                                c;
                            c = c.next;
                            d = c != b;
                            a.push(f)
                        }
                    } else a = DCEL.circumference(null, this.faces);
                    this.border = a;
                    this.equator = this.ridge = null
                },
                createGroups: function() {
                    for (var a = [], b = 0, c = this.faces; b < c.length;) {
                        var d = c[b];
                        ++b;
                        d.data.ward instanceof Alleys && a.push(d)
                    }
                    b = a;
                    for (a = []; 0 < b.length;) {
                        var f = this.pickFaces(b);
                        a.push(new WardGroup(f))
                    }
                    this.groups = a;
                    a = 0;
                    for (b = this.groups; a < b.length;)
                        if (f = b[a], ++a, 1 < f.faces.length) {
                            c = [];
                            for (var h = 0, k = f.border; h < k.length;) d = k[h], ++h, d = d.origin, Z.some(d.edges, function(a) {
                                return null != a.data ? a.data != Tc.HORIZON :
                                    !1
                            }) ? c.push(d.point) : Z.some(this.city.cellsByVertex(d), function(a) {
                                return !(a.ward instanceof Alleys)
                            }) && c.push(d.point);
                            f = EdgeChain.toPoly(f.border);
                            c = PolyUtils.smooth(f, c, 2);
                            PolyCore.set(f, c)
                        }
                },
                pickFaces: function(a) {
                    for (var b = [Z.pick(a)];;) {
                        var c = b.length;
                        1 < a.length ? (c = (c - 3) / c, null == c && (c = .5), c = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < c) : c = !1;
                        if (c) break;
                        c = [];
                        for (var d = 0; d < b.length;) {
                            var f = b[d];
                            ++d;
                            for (var h = f = f.halfEdge, k = !0; k;) {
                                var n = h;
                                h = h.next;
                                k = h != f;
                                null == n.data && (n = n.twin.face, -1 != a.indexOf(n) && -1 == b.indexOf(n) &&
                                    c.push(n))
                            }
                        }
                        if (Z.isEmpty(c)) break;
                        else c = Z.pick(c), N.remove(a, c), b.push(c)
                    }
                    1 < b.length && District.check4holes(b) && (hb.trace("Hole in a group, we need to split it", {
                        fileName: "Source/com/watabou/mfcg/model/District.hx",
                        lineNumber: 175,
                        className: "com.watabou.mfcg.model.District",
                        methodName: "pickFaces"
                    }), Z.addAll(a, b.slice(1)), b = [b[0]]);
                    return b
                },
                getRidge: function() {
                    var a = EdgeChain.toPoly(this.border).slice();
                    PolyUtils.simplify(a);
                    if ("Curved" == State.get("districts", "Curved")) return null == this.ridge && (this.ridge = Xk.build(a)), this.ridge;
                    null == this.equator && (this.equator = Equator.build(a));
                    return this.equator
                },
                __class__: District
            };
            var DistrictBuilder = function(a) {
                this.model = a
            };
            g["com.watabou.mfcg.model.DistrictBuilder"] = DistrictBuilder;
            DistrictBuilder.__name__ = "com.watabou.mfcg.model.DistrictBuilder";
            DistrictBuilder.prototype = {
                build: function() {
                    for (var a = this, b = [], c = 0, d = this.model.cells; c < d.length;) {
                        var f = d[c];
                        ++c;
                        f.withinCity && b.push(f)
                    }
                    this.city = b;
                    this.unassigned = this.city.slice();
                    this.districts = [];
                    var h = [],
                        k = function(a, b) {
                            h.push({
                                vertex: a,
                                type: b
                            })
                        };
                    d = function(b, c) {
                        h.push({
                            patch: b,
                            type: null != c ?
                                c : a.getType(b)
                        })
                    };
                    null != this.model.citadel && k(va.__cast(this.model.citadel.ward, Castle).wall.gates[0], lc.CASTLE(this.model.citadel));
                    null != this.model.plaza ? d(this.model.plaza, lc.CENTER(this.model.plaza)) : k(this.model.dcel.vertices.h[this.model.center.__id__], lc.CENTER(null));
                    b = 0;
                    for (c = this.unassigned; b < c.length;) f = c[b], ++b, f.ward instanceof Park && d(f, lc.PARK);
                    if (null != this.model.wall)
                        for (b = 0, c = this.model.wall.gates; b < c.length;) {
                            var n = c[b];
                            ++b;
                            k(n, lc.GATE(n))
                        }
                    b = 0;
                    for (c = this.model.canals; b < c.length;) {
                        n = c[b];
                        ++b;
                        var p = n.bridges;
                        f = p;
                        for (p = p.keys(); p.hasNext();) {
                            var g = p.next();
                            f.get(g);
                            k(g, lc.BRIDGE(g))
                        }
                        f = Z.random(n.course);
                        f.face.data.withinCity && d(f.face.data, lc.BANK);
                        n = Z.random(n.course).twin;
                        n.face.data.withinCity && d(n.face.data, lc.BANK)
                    }
                    b = 0;
                    for (c = this.unassigned; b < c.length;)
                        if (f = c[b], ++b, f.landing && f.ward instanceof Alleys) {
                            d(f, lc.DOCKS);
                            break
                        } b = 0;
                    for (c = h.length; b < c;) b++, d(Z.random(this.city));
                    b = Math.sqrt(this.city.length) | 0;
                    for (h = Z.subset(h, b); h.length < b;) d(Z.random(this.city));
                    b = [];
                    c = 0;
                    for (d = h; c < d.length;) k =
                        d[c], ++c, k.type == lc.DOCKS && b.push(k);
                    1 < b.length && (Z.removeAll(h, b), h.push(Z.random(b)));
                    for (b = 0; b < h.length;) c = h[b], ++b, null != c.patch ? this.fromPatch(c.patch, c.type) : this.fromVertex(c.vertex, c.type);
                    this.growAll();
                    b = 0;
                    for (c = this.districts; b < c.length;) d = c[b], ++b, d.updateGeometry(), d.createGroups();
                    this.sort(this.districts, this.model.cells[0]);
                    District.updateColors(this.districts)
                },
                fromPatch: function(a, b) {
                    return null == a.district ? (b = new District([a], b), this.districts.push(b), N.remove(this.unassigned, a), b) : null
                },
                fromVertex: function(a,
                    b, c) {
                    null == c && (c = !0);
                    var d = this.model.cellsByVertex(a);
                    d = Z.intersect(d, this.city);
                    a = [];
                    for (var f = 0; f < d.length;) {
                        var h = d[f];
                        ++f;
                        null == h.district && a.push(h)
                    }
                    if (0 == a.length || c && a.length < d.length) return null;
                    b = new District(a, b);
                    this.districts.push(b);
                    Z.removeAll(this.unassigned, a);
                    return b
                },
                getType: function(a) {
                    return a.ward instanceof Castle ? lc.CASTLE(a) : a.ward instanceof Park ? lc.PARK : a.landing && a.ward instanceof Alleys ? lc.DOCKS : -1 != this.model.inner.indexOf(a) ? lc.REGULAR : lc.SPRAWL
                },
                growAll: function() {
                    for (var a = [], b =
                            0, c = this.districts; b < c.length;) {
                        var d = c[b];
                        ++b;
                        a.push(this.getGrower(d))
                    }
                    for (b = a; 0 < b.length;)
                        for (c = Z.shuffle(b), a = 0; a < c.length;)
                            if (d = c[a], ++a, d.grow(this.unassigned) || N.remove(b, d), 0 == this.unassigned.length) return;
                    for (; 0 < this.unassigned.length;)
                        for (a = Z.random(this.unassigned), a = this.fromPatch(a, this.getType(a)), d = this.getGrower(a); d.grow(this.unassigned););
                },
                getGrower: function(a) {
                    switch (a.type._hx_index) {
                        case 2:
                            return new DocksGrower(a);
                        case 6:
                            return new ParkGrower(a);
                        default:
                            return new Grower(a)
                    }
                },
                sort: function(a, b) {
                    for (var c =
                            new pa, d = 0; d < a.length;) {
                        var f = a[d];
                        ++d;
                        null == f.equator && (f.equator = Equator.build(EdgeChain.toPoly(f.border)));
                        c.set(f, GeomUtils.lerp(f.equator[0], f.equator[1]))
                    }
                    var h = c,
                        k = PolyCore.centroid(b.shape);
                    a.sort(function(a, b) {
                        a = I.distance(k, h.h[a.__id__]);
                        b = I.distance(k, h.h[b.__id__]);
                        b = a - b;
                        return 0 == b ? 0 : 0 > b ? -1 : 1
                    });
                    for (c = 0; c < a.length;)
                        if (f = a[c], ++c, -1 != f.faces.indexOf(b.face)) {
                            N.remove(a, f);
                            a.unshift(f);
                            break
                        } var n = a.shift();
                    for (b = [n]; 0 < a.length;) c = Z.min(a, function(a) {
                        return I.distance(h.h[a.__id__], h.h[n.__id__])
                    }), n = I.distance(h.h[n.__id__],
                        h.h[c.__id__]) > I.distance(k, h.h[a[0].__id__]) ? a[0] : c, N.remove(a, n), b.push(n);
                    for (c = 0; c < b.length;) d = b[c], ++c, a.push(d)
                },
                __class__: DistrictBuilder
            };
            var Grower = function(a) {
                this.district = a;
                switch (a.type._hx_index) {
                    case 1:
                        a = .1;
                        break;
                    case 3:
                        a = .1;
                        break;
                    case 4:
                        a = .1;
                        break;
                    case 5:
                        a = .5;
                        break;
                    default:
                        a = 1
                }
                this.rate = a
            };
            g["com.watabou.mfcg.model.Grower"] = Grower;
            Grower.__name__ = "com.watabou.mfcg.model.Grower";
            Grower.prototype = {
                grow: function(a) {
                    if (0 == this.rate) return !1;
                    var b = 1 - this.rate;
                    null == b && (b = .5);
                    if ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 <
                        b) return !0;
                    for (var c = [], d = 0, f = this.district.faces; d < f.length;) {
                        var h = f[d];
                        ++d;
                        for (var k = h.halfEdge, n = k, p = !0; p;) {
                            var g = n;
                            n = n.next;
                            p = n != k;
                            b = g;
                            g = b.twin;
                            g = null != g ? g.face.data : null; - 1 != a.indexOf(g) && (b = this.validatePatch(h.data, g) * this.validateEdge(b.data), null == b && (b = .5), (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < b && c.push(g))
                        }
                    }
                    return 0 < c.length ? (c = Z.random(c), this.district.faces.push(c.face), c.district = this.district, N.remove(a, c), !0) : !1
                },
                validatePatch: function(a, b) {
                    return a.landing == b.landing ? 1 : 0
                },
                validateEdge: function(a) {
                    if (null != a) switch (a._hx_index) {
                        case 2:
                            return .9;
                        case 3:
                            return 0;
                        case 4:
                            return 0;
                        default:
                            return 1
                    } else return 1
                },
                __class__: Grower
            };
            var DocksGrower = function(a) {
                Grower.call(this, a)
            };
            g["com.watabou.mfcg.model.DocksGrower"] = DocksGrower;
            DocksGrower.__name__ = "com.watabou.mfcg.model.DocksGrower";
            DocksGrower.__super__ = Grower;
            DocksGrower.prototype = v(Grower.prototype, {
                validatePatch: function(a, b) {
                    return b.landing && b.ward instanceof Alleys ? 1 : 0
                },
                __class__: DocksGrower
            });
            var ParkGrower = function(a) {
                Grower.call(this, a)
            };
            g["com.watabou.mfcg.model.ParkGrower"] = ParkGrower;
            ParkGrower.__name__ = "com.watabou.mfcg.model.ParkGrower";
            ParkGrower.__super__ = Grower;
            ParkGrower.prototype = v(Grower.prototype, {
                validatePatch: function(a, b) {
                    return b.ward instanceof Park ? 1 : 0
                },
                __class__: ParkGrower
            });
            var Noise = function() {
                this.components = []
            };
            g["com.watabou.utils.Noise"] = Noise;
            Noise.__name__ = "com.watabou.utils.Noise";
            Noise.fractal = function(a, b, c) {
                null == c && (c = .5);
                null == b && (b = 1);
                null == a && (a = 1);
                for (var d = new Noise, f = 1, h = 0; h < a;) {
                    h++;
                    var k = new Perlin(C.seed = 48271 * C.seed % 2147483647 | 0);
                    k.gridSize = b;
                    k.amplitude = f;
                    d.components.push(k);
                    b *= 2;
                    f *= c
                }
                return d
            };
            Noise.prototype = {
                get: function(a, b) {
                    for (var c = 0, d = 0, f =
                            this.components; d < f.length;) {
                        var h = f[d];
                        ++d;
                        c += h.get(a, b)
                    }
                    return c
                },
                __class__: Noise
            };
            var C = function() {};
            g["com.watabou.utils.Random"] = C;
            C.__name__ = "com.watabou.utils.Random";
            C.reset = function(a) {
                null == a && (a = -1);
                C.seed = -1 != a ? a : (new Date).getTime() % 2147483647 | 0
            };
            C.save = function() {
                return C.saved = C.seed
            };
            C.restore = function(a) {
                null == a && (a = -1); - 1 != a ? C.seed = a : -1 != C.saved && (C.seed = C.saved, C.saved = -1)
            };
            C.float = function() {
                return (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647
            };
            var Perlin = function(a) {
                this.offsetX = this.offsetY =
                    0;
                this.gridSize = this.amplitude = 1;
                for (var b = [], c = 0; 256 > c;) {
                    var d = c++;
                    b.push(Perlin.permutation[(d + a) % 256])
                }
                this.p = b;
                this.p = this.p.concat(this.p);
                if (null == Perlin.smooth) {
                    b = [];
                    for (c = 0; 4096 > c;) d = c++, a = d / 4096, b.push(a * a * a * (a * (6 * a - 15) + 10));
                    Perlin.smooth = b
                }
            };
            g["com.watabou.utils.Perlin"] = Perlin;
            Perlin.__name__ = "com.watabou.utils.Perlin";
            Perlin.prototype = {
                get: function(a, b) {
                    a = a * this.gridSize + this.offsetX;
                    0 > a && (a += 256);
                    b = b * this.gridSize + this.offsetY;
                    0 > b && (b += 256);
                    var c = Math.floor(a),
                        d = c + 1,
                        f = a - c,
                        h = Perlin.smooth[4096 * f | 0];
                    a = Math.floor(b);
                    var k = a + 1,
                        n = b - a,
                        p = Perlin.smooth[4096 * n | 0];
                    b = this.p[this.p[d] + a];
                    var g = this.p[this.p[c] + k];
                    d = this.p[this.p[d] + k];
                    switch (this.p[this.p[c] + a] & 3) {
                        case 0:
                            c = f + n;
                            break;
                        case 1:
                            c = f - n;
                            break;
                        case 2:
                            c = -f + n;
                            break;
                        case 3:
                            c = -f - n;
                            break;
                        default:
                            c = 0
                    }
                    a = f - 1;
                    switch (b & 3) {
                        case 0:
                            b = a + n;
                            break;
                        case 1:
                            b = a - n;
                            break;
                        case 2:
                            b = -a + n;
                            break;
                        case 3:
                            b = -a - n;
                            break;
                        default:
                            b = 0
                    }
                    k = c + (b - c) * h;
                    b = n - 1;
                    switch (g & 3) {
                        case 0:
                            c = f + b;
                            break;
                        case 1:
                            c = f - b;
                            break;
                        case 2:
                            c = -f + b;
                            break;
                        case 3:
                            c = -f - b;
                            break;
                        default:
                            c = 0
                    }
                    a = f - 1;
                    b = n - 1;
                    switch (d & 3) {
                        case 0:
                            b = a + b;
                            break;
                        case 1:
                            b =
                                a - b;
                            break;
                        case 2:
                            b = -a + b;
                            break;
                        case 3:
                            b = -a - b;
                            break;
                        default:
                            b = 0
                    }
                    return this.amplitude * (k + (c + (b - c) * h - k) * p)
                },
                __class__: Perlin
            };
            var Forester = function() {};
            g["com.watabou.mfcg.model.Forester"] = Forester;
            Forester.__name__ = "com.watabou.mfcg.model.Forester";
            Forester.fillArea = function(a, b) {
                null == b && (b = 1);
                a = Forester.pattern.fill(new FillablePoly(a));
                for (var c = [], d = 0; d < a.length;) {
                    var f = a[d];
                    ++d;
                    (Forester.noise.get(f.x, f.y) + 1) / 2 < b && c.push(f)
                }
                return c
            };
            Forester.fillLine = function(a, b, c) {
                null == c && (c = 1);
                for (var d = Math.ceil(I.distance(a, b) / 3), f = [], h = 0; h < d;) {
                    var k = h++;
                    k = GeomUtils.lerp(a,
                        b, (k + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / d);
                    (Forester.noise.get(k.x, k.y) + 1) / 2 < c && f.push(k)
                }
                return f
            };
            var Landmark = function(a, b, c) {
                null == c && (c = "Landmark");
                this.model = a;
                this.pos = b;
                this.name = c;
                this.assign()
            };
            g["com.watabou.mfcg.model.Landmark"] = Landmark;
            Landmark.__name__ = "com.watabou.mfcg.model.Landmark";
            Landmark.prototype = {
                assign: function() {
                    for (var a = 0, b = this.model.cells; a < b.length;) {
                        var c = b[a];
                        ++a;
                        if (this.assignPoly(c.shape)) break
                    }
                },
                assignPoly: function(a) {
                    if (PolyBounds.rect(a).containsPoint(this.pos)) {
                        var b = a.length;
                        this.p0 =
                            a[0];
                        for (var c = 2; c < b;) {
                            var d = c++;
                            this.p1 = a[d - 1];
                            this.p2 = a[d];
                            d = GeomUtils.barycentric(this.p0, this.p1, this.p2, this.pos);
                            if (0 <= d.x && 0 <= d.y && 0 <= d.z) return this.i0 = d.x, this.i1 = d.y, this.i2 = d.z, !0
                        }
                    }
                    return !1
                },
                update: function() {
                    var a = this.p0,
                        b = this.i0,
                        c = new I(a.x * b, a.y * b);
                    a = this.p1;
                    b = this.i1;
                    c = c.add(new I(a.x * b, a.y * b));
                    a = this.p2;
                    b = this.i2;
                    this.pos = c.add(new I(a.x * b, a.y * b))
                },
                __class__: Landmark
            };
            var je = function(a) {
                null == a && (a = []);
                this.valueClasses = a;
                this.slots = zd.NIL;
                this.priorityBased = !1
            };
            g["msignal.Signal"] = je;
            je.__name__ =
                "msignal.Signal";
            je.prototype = {
                add: function(a) {
                    return this.registerListener(a)
                },
                addOnce: function(a) {
                    return this.registerListener(a, !0)
                },
                addWithPriority: function(a, b) {
                    null == b && (b = 0);
                    return this.registerListener(a, !1, b)
                },
                addOnceWithPriority: function(a, b) {
                    null == b && (b = 0);
                    return this.registerListener(a, !0, b)
                },
                remove: function(a) {
                    var b = this.slots.find(a);
                    if (null == b) return null;
                    this.slots = this.slots.filterNot(a);
                    return b
                },
                removeAll: function() {
                    this.slots = zd.NIL
                },
                registerListener: function(a, b, c) {
                    null == c && (c =
                        0);
                    null == b && (b = !1);
                    return this.registrationPossible(a, b) ? (a = this.createSlot(a, b, c), this.priorityBased || 0 == c || (this.priorityBased = !0), this.slots = this.priorityBased || 0 != c ? this.slots.insertWithPriority(a) : this.slots.prepend(a), a) : this.slots.find(a)
                },
                registrationPossible: function(a, b) {
                    return this.slots.nonEmpty && null != this.slots.find(a) ? !1 : !0
                },
                createSlot: function(a, b, c) {
                    return null
                },
                get_numListeners: function() {
                    return this.slots.get_length()
                },
                __class__: je,
                __properties__: {
                    get_numListeners: "get_numListeners"
                }
            };
            var Nc = function() {
                je.call(this)
            };
            g["msignal.Signal0"] = Nc;
            Nc.__name__ = "msignal.Signal0";
            Nc.__super__ = je;
            Nc.prototype = v(je.prototype, {
                dispatch: function() {
                    for (var a = this.slots; a.nonEmpty;) a.head.execute(), a = a.tail
                },
                createSlot: function(a, b, c) {
                    null == c && (c = 0);
                    null == b && (b = !1);
                    return new gi(this, a, b, c)
                },
                __class__: Nc
            });
            var zd = function(a, b) {
                this.nonEmpty = !1;
                null == a && null == b ? this.nonEmpty = !1 : null != a && (this.head = a, this.tail = null == b ? zd.NIL : b, this.nonEmpty = !0)
            };
            g["msignal.SlotList"] = zd;
            zd.__name__ = "msignal.SlotList";
            zd.prototype = {
                get_length: function() {
                    if (!this.nonEmpty) return 0;
                    if (this.tail == zd.NIL) return 1;
                    for (var a = 0, b = this; b.nonEmpty;) ++a, b = b.tail;
                    return a
                },
                prepend: function(a) {
                    return new zd(a, this)
                },
                insertWithPriority: function(a) {
                    if (!this.nonEmpty) return new zd(a);
                    var b = a.priority;
                    if (b >= this.head.priority) return this.prepend(a);
                    for (var c = new zd(this.head), d = c, f = this.tail; f.nonEmpty;) {
                        if (b > f.head.priority) return d.tail = f.prepend(a), c;
                        d = d.tail = new zd(f.head);
                        f = f.tail
                    }
                    d.tail = new zd(a);
                    return c
                },
                filterNot: function(a) {
                    if (!this.nonEmpty ||
                        null == a) return this;
                    if (this.head.listener == a) return this.tail;
                    for (var b = new zd(this.head), c = b, d = this.tail; d.nonEmpty;) {
                        if (d.head.listener == a) return c.tail = d.tail, b;
                        c = c.tail = new zd(d.head);
                        d = d.tail
                    }
                    return this
                },
                find: function(a) {
                    if (!this.nonEmpty) return null;
                    for (var b = this; b.nonEmpty;) {
                        if (b.head.listener == a) return b.head;
                        b = b.tail
                    }
                    return null
                },
                __class__: zd,
                __properties__: {
                    get_length: "get_length"
                }
            };
            var ec = function(a) {
                je.call(this, [a])
            };
            g["msignal.Signal1"] = ec;
            ec.__name__ = "msignal.Signal1";
            ec.__super__ =
                je;
            ec.prototype = v(je.prototype, {
                dispatch: function(a) {
                    for (var b = this.slots; b.nonEmpty;) b.head.execute(a), b = b.tail
                },
                createSlot: function(a, b, c) {
                    null == c && (c = 0);
                    null == b && (b = !1);
                    return new hi(this, a, b, c)
                },
                __class__: ec
            });
            var ModelDispatcher = function() {};
            g["com.watabou.mfcg.model.ModelDispatcher"] = ModelDispatcher;
            ModelDispatcher.__name__ = "com.watabou.mfcg.model.ModelDispatcher";
            var Topology = function(a) {
                this.graph = new Graph;
                this.pt2node = new pa;
                for (var b = 0; b < a.length;) {
                    var c = a[b];
                    ++b;
                    for (var d = c = c.face.halfEdge, f = !0; f;) {
                        var h = d;
                        d = d.next;
                        f = d != c;
                        if (null ==
                            h.data) {
                            var k = this.getNode(h.origin),
                                n = this.getNode(h.next.origin);
                            k.link(n, I.distance(h.origin.point, h.next.origin.point), !1)
                        }
                    }
                }
            };
            g["com.watabou.mfcg.model.Topology"] = Topology;
            Topology.__name__ = "com.watabou.mfcg.model.Topology";
            Topology.prototype = {
                getNode: function(a) {
                    if (null != this.pt2node.h.__keys__[a.__id__]) return this.pt2node.h[a.__id__];
                    var b = this.pt2node,
                        c = this.graph.add(new Node(a));
                    b.set(a, c);
                    return c
                },
                buildPath: function(a, b) {
                    var c = this.pt2node.h[b.__id__];
                    if (null == this.pt2node.h[a.__id__] || null == c) return null;
                    a = this.graph.aStar(this.pt2node.h[a.__id__], this.pt2node.h[b.__id__]);
                    if (null == a) return null;
                    b = [];
                    for (c = 0; c < a.length;) {
                        var d = a[c];
                        ++c;
                        b.push(d.data)
                    }
                    return b
                },
                excludePoints: function(a) {
                    for (var b = 0; b < a.length;) {
                        var c = a[b];
                        ++b;
                        c = this.pt2node.h[c.__id__];
                        null != c && c.unlinkAll()
                    }
                },
                excludePolygon: function(a) {
                    for (var b = 0; b < a.length;) {
                        var c = a[b];
                        ++b;
                        var d = this.pt2node.h[c.origin.__id__];
                        c = this.pt2node.h[c.next.origin.__id__];
                        null != d && null != c && d.unlink(c)
                    }
                },
                __class__: Topology
            };
            var UnitSystem = function(a, b, c) {
                this.unit = a;
                this.iu2unit = b;
                this.sub = c
            };
            g["com.watabou.mfcg.model.UnitSystem"] = UnitSystem;
            UnitSystem.__name__ = "com.watabou.mfcg.model.UnitSystem";
            UnitSystem.__properties__ = {
                set_current: "set_current",
                get_current: "get_current"
            };
            UnitSystem.toggle = function() {
                UnitSystem.set_current(UnitSystem.get_current() == UnitSystem.metric ? UnitSystem.imperial : UnitSystem.metric)
            };
            UnitSystem.get_current = function() {
                return UnitSystem._current
            };
            UnitSystem.set_current = function(a) {
                UnitSystem._current = a;
                ModelDispatcher.unitsChanged.dispatch();
                return UnitSystem._current
            };
            UnitSystem.prototype = {
                measure: function(a) {
                    for (var b = this;;) {
                        var c = a / b.iu2unit;
                        if (1 > c && null != b.sub) b =
                            this.sub;
                        else return {
                            value: c,
                            system: b
                        }
                    }
                },
                getPlank: function(a, b) {
                    a = a.get_scaleX();
                    for (var c, d, f = this;;)
                        if (d = f.iu2unit * a, c = Math.pow(10, Math.ceil(Math.log(b / d) / Math.log(10))), d *= c, d > 5 * b ? c /= 5 : d > 4 * b ? c /= 4 : d > 2 * b && (c /= 2), 1 >= c && null != f.sub) f = f.sub;
                        else break;
                    return c * f.iu2unit
                },
                __class__: UnitSystem
            };
            var Block = function(a, b, c) {
                null == c && (c = !1);
                this.cacheOBB = new pa;
                this.cacheArea = new pa;
                this.group = a;
                this.shape = b;
                a = a.district.alleys;
                c ? this.lots = [b] : (State.get("no_triangles", !1), State.get("lots_method", "Twisted"), this.lots = $k.createLots(this,
                    a));
                Main.preview || "Offset" != State.get("processing") || this.indentFronts(this.lots)
            };
            g["com.watabou.mfcg.model.blocks.Block"] = Block;
            Block.__name__ = "com.watabou.mfcg.model.blocks.Block";
            Block.prototype = {
                createRects: function() {
                    this.rects = [];
                    for (var a = this.group.district.alleys.inset, b = this.shape.length, c = 0, d = this.lots; c < d.length;) {
                        var f = d[c];
                        ++c;
                        var h = -1,
                            k = !1,
                            n = f.length;
                        if (this.isRectangle(f)) k = !0;
                        else
                            for (var p = 0, g = n; p < g;) {
                                for (var q = p++, m = f[q], u = f[(q + 1) % n], r = -1, l = 0, x = b; l < x;) {
                                    var D = l++;
                                    if (GeomUtils.converge(m, u, this.shape[D],
                                            this.shape[(D + 1) % b])) {
                                        r = q;
                                        break
                                    }
                                }
                                if (-1 != r)
                                    if (-1 != h) {
                                        k = !0;
                                        break
                                    } else h = r
                            }
                        k || (null != this.cacheArea.h.__keys__[f.__id__] ? k = this.cacheArea.h[f.__id__] : (k = this.cacheArea, n = PolyCore.area(f), k.set(f, n), k = n), h = -1 != h ? PolyBounds.lir(f, h) : PolyBounds.lira(f), k = Math.max(1.2, Math.sqrt(k) / 2), f = I.distance(h[0], h[1 % h.length]) >= k && I.distance(h[1], h[2 % h.length]) >= k ? h : f);
                        if ("Shrink" == State.get("processing") && (h = a * (1 - Math.abs(((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 |
                                0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 2 - 1)), .3 < h)) {
                            k = f.length;
                            n = [];
                            p = 0;
                            for (g = k; p < g;) {
                                m = p++;
                                q = f[m];
                                m = f[(m + 1) % k];
                                u = !1;
                                r = 0;
                                for (l = b; r < l;)
                                    if (x = r++, GeomUtils.converge(q, m, this.shape[x], this.shape[(x + 1) % b])) {
                                        u = !0;
                                        break
                                    } n.push(u ? 0 : h)
                            }
                            f = PolyCut.shrink(f, n)
                        }
                        this.rects.push(f)
                    }
                },
                isRectangle: function(a) {
                    if (4 != a.length) return !1;
                    var b = PolyCore.area(a);
                    a = PolyCore.rectArea(PolyBounds.obb(a));
                    return .75 < b / a
                },
                createBuildings: function() {
                    var a = this;
                    null == this.rects && this.createRects();
                    var b = this.group.district.alleys,
                        c = b.minSq /
                        4 * b.shapeFactor;
                    this.buildings = [];
                    b = 0;
                    for (var d = this.rects; b < d.length;) {
                        var f = [d[b]];
                        ++b;
                        var h = function(b) {
                            return function(d) {
                                d = Building.create(d, c, !0, null, .6);
                                a.buildings.push(null != d ? d : b[0])
                            }
                        }(f);
                        if (4 < f[0].length) {
                            f = f[0].slice();
                            do PolyCore.simplifyClosed(f); while (4 < f.length);
                            h(f)
                        } else 4 == f[0].length ? h(f[0]) : this.buildings.push(f[0])
                    }
                },
                filterInner: function(a) {
                    for (var b = [], c = 0; c < a.length;) {
                        var d = a[c];
                        ++c;
                        a: {
                            var f = d;
                            var h = this.shape;h = h[h.length - 1];
                            for (var k = 0, n = this.shape.length; k < n;) {
                                var p = k++,
                                    g = h;
                                h = this.shape[p];
                                p = h.x - g.x;
                                var q = h.y - g.y,
                                    m = p * p + q * q;
                                if (!(1E-9 > m))
                                    for (var u = 0, r = f.length; u < r;) {
                                        var l = u++;
                                        l = f[l];
                                        var x = ((l.x - g.x) * p + (l.y - g.y) * q) / m;
                                        if (!(0 > x || 1 < x) && 1E-9 > I.distance(l, GeomUtils.lerp(g, h, x))) {
                                            f = !1;
                                            break a
                                        }
                                    }
                            }
                            f = !0
                        }
                        f && b.push(d)
                    }
                    this.courtyard = b;
                    return Z.difference(a, this.courtyard)
                },
                indentFronts: function(a) {
                    for (var b = 0, c = a.length; b < c;) {
                        var d = b++,
                            f = a[d];
                        if (null != this.cacheArea.h.__keys__[f.__id__]) var h = this.cacheArea.h[f.__id__];
                        else {
                            h = this.cacheArea;
                            var k = PolyCore.area(f);
                            h.set(f, k);
                            h = k
                        }
                        h = Math.min(Math.sqrt(h) / 3, 1.2) *
                            ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647);
                        .5 > h || (k = PolyCore.center(f), k = (null == this.center ? this.center = PolyCore.centroid(this.shape) : this.center).subtract(k), k.normalize(h), h = PolyTransform.translate(this.shape, k.x, k.y), f = PolyBool.and(f, h), null != f && 3 <= f.length && (a[d] = f))
                    }
                },
                spawnTrees: function() {
                    var a = [];
                    if (null != this.courtyard) {
                        var b = this.group.district.greenery;
                        this.group.urban || (b *= .1);
                        for (var c = 0, d = this.courtyard; c < d.length;) {
                            var f = d[c];
                            ++c;
                            f = Forester.fillArea(f, b);
                            for (var h = 0; h < f.length;) {
                                var k = f[h];
                                ++h;
                                a.push(k)
                            }
                        }
                    }
                    return a
                },
                __class__: Block
            };
            var $k = function() {};
            g["com.watabou.mfcg.model.blocks.TwistedBlock"] = $k;
            $k.__name__ = "com.watabou.mfcg.model.blocks.TwistedBlock";
            $k.createLots = function(a, b) {
                var c = new Bisector(a.shape, b.minSq, Math.max(4 * b.sizeChaos, 1.2));
                c.minTurnOffset = .5;
                var d = c.partition();
                d = a.filterInner(d);
                b = b.minSq / 4;
                c = [];
                for (var f = 0; f < d.length;) {
                    var h = d[f];
                    ++f;
                    if (null != a.cacheArea.h.__keys__[h.__id__]) var k = a.cacheArea.h[h.__id__];
                    else {
                        k = a.cacheArea;
                        var n = PolyCore.area(h);
                        k.set(h, n);
                        k = n
                    }
                    if (4 > h.length || k < b) k = !1;
                    else {
                        if (null !=
                            a.cacheOBB.h.__keys__[h.__id__]) var p = a.cacheOBB.h[h.__id__];
                        else n = a.cacheOBB, p = PolyBounds.obb(h), n.set(h, p);
                        n = I.distance(p[0], p[1]);
                        p = I.distance(p[1], p[2]);
                        k = 1.2 <= n && 1.2 <= p && .5 < k / (n * p)
                    }
                    k && c.push(h)
                }
                return c
            };
            var Ward = function(a, b) {
                this.model = a;
                this.patch = b;
                b.ward = this
            };
            g["com.watabou.mfcg.model.wards.Ward"] = Ward;
            Ward.__name__ = "com.watabou.mfcg.model.wards.Ward";
            Ward.inset = function(a, b, c) {
                var d = PolyUtils.inset(a, b);
                if (null == d) return null;
                for (var f = c.length, h = 0; h < f;) {
                    var k = h++,
                        n = c[k],
                        p = (k + f - 1) % f;
                    n > b[k] && n > b[p] && (k = a[k],
                        n = PolyCreate.regular(9, n, (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647), PolyTransform.asTranslate(n, k.x, k.y), n = PolyBool.and(d, Z.revert(n), !0), null != n && (d = n))
                }
                return d
            };
            Ward.prototype = {
                createGeometry: function() {},
                spawnTrees: function() {
                    return null
                },
                getAvailable: function() {
                    for (var a = this.patch.shape.length, b = [], c = this.patch.face.halfEdge, d = c, f = !0; f;) {
                        var h = d;
                        d = d.next;
                        f = d != c;
                        var k = h.origin,
                            n = 0,
                            p = 0;
                        for (h = this.model.walls; p < h.length;) {
                            var g = h[p];
                            ++p;
                            n = Math.max(n, g.getTowerRadius(k))
                        }
                        g = 0;
                        for (var q = this.model.canals; g < q.length;) {
                            var m =
                                q[g];
                            ++g;
                            null != EdgeChain.edgeByOrigin(m.course, k) && (n = Math.max(n, m.width))
                        }
                        b.push(n)
                    }
                    p = [];
                    d = c = this.patch.face.halfEdge;
                    for (f = !0; f;) h = d, d = d.next, f = d != c, k = h, p.push(k);
                    c = p;
                    p = [];
                    h = 0;
                    for (g = a; h < g;) {
                        a = h++;
                        k = c[a];
                        q = 0;
                        for (d = this.model.canals; q < d.length;) m = d[q], ++q, null != EdgeChain.edgeByOrigin(m.course, k.origin) && (b[a] = m.width / 2 + 1.2, k.origin == m.course[0].origin && (b[a] += 1.2));
                        if (null == k.data) p.push((k.twin.face.data == this.model.plaza ? 2 : 1.2) / 2);
                        else {
                            switch (k.data._hx_index) {
                                case 1:
                                    a = this.patch.landing ? 2 : 1.2;
                                    break;
                                case 2:
                                    a =
                                        1;
                                    break;
                                case 3:
                                    a = CurtainWall.THICKNESS / 2 + 1.2;
                                    break;
                                case 4:
                                    a = this.model.getCanalWidth(k) / 2 + 1.2;
                                    break;
                                default:
                                    a = 0
                            }
                            p.push(a)
                        }
                    }
                    return Ward.inset(this.patch.shape, p, b)
                },
                getLabel: function() {
                    return null != this.patch.district ? this.patch.district.name : null
                },
                getColor: function() {
                    return null != this.patch.district ? this.patch.district.color : 0
                },
                onContext: function(a, b, c) {},
                __class__: Ward
            };
            var Alleys = function(a, b) {
                Ward.call(this, a, b)
            };
            g["com.watabou.mfcg.model.wards.Alleys"] = Alleys;
            Alleys.__name__ = "com.watabou.mfcg.model.wards.Alleys";
            Alleys.__super__ =
                Ward;
            Alleys.prototype = v(Ward.prototype, {
                createGeometry: function() {
                    this.group.core == this.patch.face && this.group.createGeometry();
                    this.trees = null
                },
                spawnTrees: function() {
                    if (this.group.core == this.patch.face && null == this.trees) {
                        this.trees = [];
                        for (var a = 0, b = this.group.blocks; a < b.length;) {
                            var c = b[a];
                            ++a;
                            var d = this.trees;
                            c = c.spawnTrees();
                            for (var f = 0; f < c.length;) {
                                var h = c[f];
                                ++f;
                                d.push(h)
                            }
                        }
                    }
                    return this.trees
                },
                onContext: function(a, b, c) {
                    var d = State.get("display_mode", "Lots");
                    if ("Block" != d) {
                        c = new I(b, c);
                        for (var f = 0, h = this.group.blocks; f <
                            h.length;)
                            if (b = h[f], ++f, PolyBounds.containsPoint(b.shape, c)) {
                                switch (d) {
                                    case "Complex":
                                        d = b.buildings;
                                        break;
                                    case "Simple":
                                        d = b.rects;
                                        break;
                                    default:
                                        d = b.lots
                                }
                                for (h = 0; h < d.length;)
                                    if (f = [d[h]], ++h, PolyBounds.containsPoint(f[0], c)) {
                                        c = [this.model.bp.seed];
                                        c[0] += this.model.cells.indexOf(this.patch);
                                        c[0] += this.group.blocks.indexOf(b);
                                        c[0] += d.indexOf(f[0]);
                                        a.addItem("Open in Dwellings", function(a, b) {
                                            return function() {
                                                Mansion.openInDwellings(b[0], !0, a[0])
                                            }
                                        }(c, f));
                                        break
                                    } break
                            }
                    }
                },
                __class__: Alleys
            });
            var Castle = function(a, b) {
                Ward.call(this, a, b);
                for (var c = [], d = 0, f = b.shape; d < f.length;) {
                    var h = f[d];
                    ++d;
                    Z.some(this.model.cellsByVertex(this.model.dcel.vertices.h[h.__id__]), function(a) {
                        return !a.withinCity
                    }) && c.push(h)
                }
                this.wall = new CurtainWall(!0, a, [b], c);
                this.adjustShape(this.model)
            };
            g["com.watabou.mfcg.model.wards.Castle"] = Castle;
            Castle.__name__ = "com.watabou.mfcg.model.wards.Castle";
            Castle.__super__ = Ward;
            Castle.prototype = v(Ward.prototype, {
                adjustShape: function(a) {
                    var b = this.patch.shape,
                        c = PolyCore.centroid(b),
                        d = 0,
                        f = 0,
                        h = function() {
                            d = Infinity;
                            for (var a = f = 0; a < b.length;) {
                                var h = b[a];
                                ++a;
                                var k = h.x - c.x;
                                h = h.y - c.y;
                                k = k * k + h * h;
                                d > k && (d = k);
                                f < k && (f = k)
                            }
                            d = Math.sqrt(d);
                            f = Math.sqrt(f)
                        };
                    for (h(); 10 > d;) {
                        hb.trace("Bloating the citadel...", {
                            fileName: "Source/com/watabou/mfcg/model/wards/Castle.hx",
                            lineNumber: 60,
                            className: "com.watabou.mfcg.model.wards.Castle",
                            methodName: "adjustShape"
                        });
                        var k = 2 * Math.max(15, f),
                            n = a.dcel.vertices,
                            p = n;
                        for (n = n.keys(); n.hasNext();) {
                            var g = n.next();
                            p.get(g);
                            var q = I.distance(g, c);
                            if (q < k) {
                                var m = g.subtract(c);
                                q = Math.pow(q / k, -.25);
                                PointExtender.set(g, new I(c.x + m.x * q, c.y + m.y * q))
                            }
                        }
                        h()
                    }
                    h =
                        this.wall.gates[0];
                    a = [h.point];
                    2 == h.edges.length && (a.push(h.edges[0].next.origin.point), a.push(h.edges[1].next.origin.point));
                    for (h = PolyCore.compactness(b); .75 > h;) {
                        hb.trace("Equalizing... " + h, {
                            fileName: "Source/com/watabou/mfcg/model/wards/Castle.hx",
                            lineNumber: 84,
                            className: "com.watabou.mfcg.model.wards.Castle",
                            methodName: "adjustShape"
                        });
                        this.equalize(c, .2, a);
                        k = PolyCore.compactness(b);
                        if (.001 > Math.abs(k - h)) throw X.thrown("Bad citadel shape!");
                        h = k
                    }
                },
                equalize: function(a, b, c) {
                    for (var d = this.patch.shape, f = d.length,
                            h = d[0].subtract(a), k = 1, n = f; k < n;) {
                        var p = k++,
                            g = d[p].subtract(a),
                            q = 2 * -Math.PI * p / f,
                            m = Math.sin(q);
                        q = Math.cos(q);
                        p = new I(g.x * q - g.y * m, g.y * q + g.x * m);
                        h.x += p.x;
                        h.y += p.y
                    }
                    k = 1 / f;
                    h.x *= k;
                    h.y *= k;
                    k = 0;
                    for (n = f; k < n;) p = k++, -1 == c.indexOf(d[p]) && (q = 2 * Math.PI * p / f, m = Math.sin(q), q = Math.cos(q), PointExtender.set(d[p], GeomUtils.lerp(d[p], a.add(new I(h.x * q - h.y * m, h.y * q + h.x * m)), b)))
                },
                createGeometry: function() {
                    C.restore(this.patch.seed);
                    var a = PolyCut.shrinkEq(this.patch.shape, CurtainWall.THICKNESS + 2);
                    a = PolyBounds.lira(a);
                    this.building = Building.create(a, PolyCore.area(this.patch.shape) /
                        25, null, null, .4);
                    null == this.building && (this.building = a)
                },
                getLabel: function() {
                    return "Castle"
                },
                onContext: function(a, b, c) {
                    var d = this;
                    if (PolyBounds.containsPoint(this.building, new I(b, c))) {
                        var f = this.model.bp.seed;
                        f += this.model.cells.indexOf(this.patch);
                        a.addItem("Open in Dwellings", function() {
                            Mansion.openInDwellings(d.building, !0, f)
                        })
                    }
                },
                __class__: Castle
            });
            var Cathedral = function(a, b) {
                Ward.call(this, a, b)
            };
            g["com.watabou.mfcg.model.wards.Cathedral"] = Cathedral;
            Cathedral.__name__ = "com.watabou.mfcg.model.wards.Cathedral";
            Cathedral.__super__ = Ward;
            Cathedral.prototype =
                v(Ward.prototype, {
                    createGeometry: function() {
                        C.restore(this.patch.seed);
                        var a = this.getAvailable();
                        if (null == a) this.building = [];
                        else {
                            a = PolyBounds.lira(a);
                            var b = Building.create(a, 20, !1, !0, .2);
                            this.building = [null != b ? b : a]
                        }
                    },
                    onContext: function(a, b, c) {
                        b = new I(b, c);
                        c = 0;
                        for (var d = this.building; c < d.length;) {
                            var f = [d[c]];
                            ++c;
                            if (PolyBounds.containsPoint(f[0], b)) {
                                var h = [this.model.bp.seed];
                                h[0] += this.model.cells.indexOf(this.patch);
                                a.addItem("Open in Dwellings", function(a, b) {
                                    return function() {
                                        Mansion.openInDwellings(b[0], !1, a[0])
                                    }
                                }(h, f))
                            }
                        }
                    },
                    __class__: Cathedral
                });
            var Farm = function(a, b) {
                Ward.call(this, a, b)
            };
            g["com.watabou.mfcg.model.wards.Farm"] = Farm;
            Farm.__name__ = "com.watabou.mfcg.model.wards.Farm";
            Farm.__super__ = Ward;
            Farm.prototype = v(Ward.prototype, {
                getAvailable: function() {
                    for (var a = this.patch.shape.length, b = [], c = this.patch.face.halfEdge, d = c, f = !0; f;) {
                        var h = d;
                        d = d.next;
                        f = d != c;
                        var k = h,
                            n = 0,
                            p = 0;
                        for (h = this.model.walls; p < h.length;) {
                            var g = h[p];
                            ++p;
                            n = Math.max(n, g.getTowerRadius(k.origin))
                        }
                        b.push(n)
                    }
                    p = [];
                    d = c = this.patch.face.halfEdge;
                    for (f = !0; f;) h = d, d = d.next, f = d != c,
                        k = h, p.push(k);
                    c = p;
                    p = [];
                    for (h = 0; h < a;) {
                        d = h++;
                        k = c[d];
                        f = 0;
                        for (n = this.model.canals; f < n.length;) g = n[f], ++f, null != EdgeChain.edgeByOrigin(g.course, k.origin) && (b[d] = g.width / 2 + 1.2, k.origin == g.course[0].origin && (b[d] += 1.2));
                        if (null == k.data) p.push(null != k.twin && k.twin.face.data.ward instanceof Farm ? 1 : 0);
                        else {
                            switch (k.data._hx_index) {
                                case 2:
                                    k = 3;
                                    break;
                                case 3:
                                    k = 2 * CurtainWall.THICKNESS;
                                    break;
                                case 4:
                                    k = this.model.getCanalWidth(k) / 2 + 1.2;
                                    break;
                                default:
                                    k = 2
                            }
                            p.push(k)
                        }
                    }
                    return Ward.inset(this.patch.shape, p, b)
                },
                createGeometry: function() {
                    C.restore(this.patch.seed);
                    var a = this.getAvailable();
                    this.furrows = [];
                    this.subPlots = this.splitField(a);
                    for (var b = [], c = a = this.patch.face.halfEdge, d = !0; d;) {
                        var f = c;
                        c = c.next;
                        d = c != a;
                        null != f.twin && f.twin.face.data.ward instanceof Ward && b.push(f)
                    }
                    if (0 < b.length) {
                        a = [];
                        c = 0;
                        for (d = this.subPlots; c < d.length;) {
                            f = d[c];
                            ++c;
                            a: {
                                var h = f;
                                for (var k = h[h.length - 1], n = 0, p = h.length; n < p;) {
                                    var g = n++,
                                        q = k;
                                    k = h[g];
                                    for (g = 0; g < b.length;) {
                                        var m = b[g];
                                        ++g;
                                        if (GeomUtils.converge(q, k, m.origin.point, m.next.origin.point)) {
                                            h = !1;
                                            break a
                                        }
                                    }
                                }
                                h = !0
                            }
                            h && a.push(f)
                        }
                        this.subPlots = a
                    }
                    a = 0;
                    for (c = this.subPlots.length; a < c;)
                        for (d = a++, b = this.subPlots[d], f = PolyBounds.obb(b), b = this.subPlots[d] = this.round(b), d = I.distance(f[0], f[1 % f.length]), h = Math.ceil(d / Farm.MIN_FURROW), d = 0, k = h; d < k;)
                            for (p = (d++ + .5) / h, n = GeomUtils.lerp(f[0], f[1], p), p = GeomUtils.lerp(f[3], f[2], p), n = PolyCut.pierce(b, n, p); 2 <= n.length;) p = n.shift(), q = n.shift(), 1.2 < I.distance(p, q) && this.furrows.push(new Segment(p, q));
                    a = [];
                    c = 0;
                    for (d = this.subPlots; c < d.length;) b = d[c], ++c, f = .2, null == f && (f = .5), (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < f && a.push(this.getHousing(b));
                    this.buildings = a;
                    this.trees = null
                },
                splitField: function(a) {
                    if (PolyCore.area(a) < Farm.MIN_SUBPLOT * (1 + Math.abs(((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 2 - 1))) return [a];
                    var b = PolyBounds.obb(a),
                        c = I.distance(b[1], b[0]) > I.distance(b[2], b[1]) ? 0 : 1,
                        d = .5 + .2 * (((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) /
                            2147483647) / 3 * 2 - 1),
                        f = .5;
                    null == f && (f = .5);
                    f = Math.PI / 2 + ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < f ? 0 : Math.PI / 8 * (((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3 * 2 - 1));
                    d = GeomUtils.lerp(b[c], b[c + 1], d);
                    b = b[c < b.length - 1 ? c + 1 : 0].subtract(b[c]);
                    c = Math.sin(f);
                    f = Math.cos(f);
                    f = d.add(new I(b.x * f - b.y * c, b.y * f + b.x * c));
                    a = PolyCut.cut(a, d, f, 2);
                    d = [];
                    for (f = 0; f < a.length;)
                        for (b = a[f], ++f, b = this.splitField(b), c = 0; c < b.length;) {
                            var h = b[c];
                            ++c;
                            d.push(h)
                        }
                    return d
                },
                round: function(a) {
                    for (var b = [], c = a.length, d = 0; d < c;) {
                        var f = d++,
                            h = a[f];
                        f = a[(f + 1) % c];
                        var k = I.distance(h, f);
                        k < 2 * Farm.MIN_FURROW ? b.push(GeomUtils.lerp(h, f)) : (b.push(GeomUtils.lerp(h, f, Farm.MIN_FURROW / k)), b.push(GeomUtils.lerp(f, h, Farm.MIN_FURROW / k)))
                    }
                    return b
                },
                getHousing: function(a) {
                    var b = 4 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647,
                        c = 2 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647,
                        d = PolyCreate.rect(b, c),
                        f = PolyAccess.longest(a),
                        h = a[f < a.length - 1 ? f + 1 : 0].subtract(a[f]);
                    h = h.clone();
                    h.normalize(1);
                    var k = h;
                    h = a[f];
                    a = a[(f + 1) %
                        a.length];
                    .5 > (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 ? (b /= 2, a = h.add(new I(k.x * b, k.y * b))) : (b /= 2, a = a.subtract(new I(k.x * b, k.y * b)));
                    h = new I(-k.y, k.x);
                    b = c / 2;
                    c = new I(h.x * b, h.y * b);
                    a.x += c.x;
                    a.y += c.y;
                    PolyTransform.asRotateYX(d, k.y, k.x);
                    PolyTransform.asAdd(d, a);
                    k = Building.create(d, 4 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647, null, null, .4);
                    return null != k ? k : d
                },
                spawnTrees: function() {
                    if (null == this.trees) {
                        this.trees = [];
                        for (var a = Math.max(this.model.bounds.width, this.model.bounds.height) * (((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 +
                                (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3), b = 0, c = this.subPlots; b < c.length;) {
                            var d = c[b];
                            ++b;
                            for (var f = d.length, h = d[f - 1], k = 0; k < f;) {
                                var n = k++,
                                    p = h;
                                h = d[n];
                                var g = 1 - GeomUtils.lerp(p, h).get_length() / a;
                                n = this.trees;
                                p = Forester.fillLine(p, h, g);
                                for (g = 0; g < p.length;) {
                                    var q = p[g];
                                    ++g;
                                    n.push(q)
                                }
                            }
                        }
                    }
                    return this.trees
                },
                getLabel: function() {
                    return "Farmland"
                },
                onContext: function(a, b, c) {
                    if ("Block" != State.get("display_mode", "Lots")) {
                        b = new I(b, c);
                        c = 0;
                        for (var d = this.buildings; c < d.length;) {
                            var f = [d[c]];
                            ++c;
                            if (PolyBounds.containsPoint(f[0], b)) {
                                var h = [this.model.bp.seed];
                                h[0] += this.model.cells.indexOf(this.patch);
                                h[0] += this.buildings.indexOf(f[0]);
                                a.addItem("Open in Dwellings", function(a, b) {
                                    return function() {
                                        Mansion.openInDwellings(b[0], !1, a[0])
                                    }
                                }(h, f))
                            }
                        }
                    }
                },
                __class__: Farm
            });
            var Harbour = function(a, b) {
                Ward.call(this, a, b)
            };
            g["com.watabou.mfcg.model.wards.Harbour"] = Harbour;
            Harbour.__name__ = "com.watabou.mfcg.model.wards.Harbour";
            Harbour.__super__ = Ward;
            Harbour.prototype = v(Ward.prototype, {
                createGeometry: function() {
                    for (var a = [], b = 0, c = this.model.canals; b <
                        c.length;) {
                        var d = c[b];
                        ++b;
                        a.push(d.course[0].origin)
                    }
                    b = [];
                    var f = this.patch.face.halfEdge;
                    do {
                        d = this.model.getNeighbour(this.patch, f.origin);
                        if (null != d && d.landing) {
                            d = f.origin.point;
                            var h = f.next.origin.point; - 1 != a.indexOf(f.origin) ? b.push(new Segment(GeomUtils.lerp(f.origin.point, f.next.origin.point, .5), h)) : -1 != a.indexOf(f.next.origin) ? b.push(new Segment(d, GeomUtils.lerp(f.origin.point, f.next.origin.point, .5))) : b.push(new Segment(d, h))
                        }
                        f = f.next
                    } while (f != this.patch.face.halfEdge);
                    if (0 < b.length) {
                        f = Z.max(b, function(a) {
                            return I.distance(a.start,
                                a.end)
                        });
                        h = I.distance(f.start, f.end);
                        d = h / 6 | 0;
                        a = 6 * (d - 1);
                        var k = (1 - a / h) / 2,
                            n = a / (d - 1) / h;
                        a = [];
                        b = 0;
                        for (c = d; b < c;) {
                            b++;
                            d = GeomUtils.lerp(f.start, f.end, k);
                            h = f.end.subtract(f.start);
                            var p = new I(-h.y, h.x);
                            h = 8;
                            null == h && (h = 1);
                            p = p.clone();
                            p.normalize(h);
                            h = d.add(p);
                            k += n;
                            a.push([d, h])
                        }
                        this.piers = a
                    } else this.piers = []
                },
                getLabel: function() {
                    return "Harbour"
                },
                __class__: Harbour
            });
            var Mansion = function() {};
            g["com.watabou.mfcg.model.wards.Mansion"] = Mansion;
            Mansion.__name__ = "com.watabou.mfcg.model.wards.Mansion";
            Mansion.openInDwellings = function(a, b, c) {
                a = new Mansion_Rect(a);
                var d = Math.round(a.len0 / 1.5),
                    f = Math.round(a.len1 / 1.5);
                11 < d && (f = Math.round(f / d * 11), d = 11);
                1 > f && (d = Math.round(d / f), f = 1);
                d = MathUtils.gatei(d, 1, 11);
                f = MathUtils.gatei(f, 1, 11);
                var h = new If(Mansion.DWELLINGS_URL);
                h.data = {
                    seed: c,
                    w: d,
                    h: f,
                    plan: Mansion.getPlan(a, d, f),
                    tags: b ? "tall" : "",
                    from: "city"
                };
                Ra.navigateToURL(h, "mfcg2pm_" + c)
            };
            Mansion.getPlan = function(a, b, c) {
                for (var d = "", f = 0, h = 0, k = 0; k < c;)
                    for (var n = k++, p = 0, g = b; p < g;) {
                        var q = p++,
                            m = a.o,
                            u = a.v0,
                            r = (b - q - 1 + .5) / b;
                        m = new I(m.x + u.x * r, m.y + u.y * r);
                        u = a.v1;
                        r = (c - 1 - n + .5) / c;
                        m = new I(m.x + u.x * r, m.y + u.y * r);
                        PolyBounds.containsPoint(a.poly,
                            m) && (f |= 1 << (h & 3));
                        if (3 == (h & 3) || q == b - 1 && n == c - 1) d += O.hex(f), f = 0;
                        ++h
                    }
                return d
            };
            var Mansion_Rect = function(a) {
                this.poly = a;
                a = PolyBounds.obb(a);
                this.o = a[0];
                this.v0 = a[1].subtract(this.o);
                this.v1 = a[3].subtract(this.o);
                this.len0 = this.v0.get_length();
                this.len1 = this.v1.get_length()
            };
            g["com.watabou.mfcg.model.wards._Mansion.Rect"] = Mansion_Rect;
            Mansion_Rect.__name__ = "com.watabou.mfcg.model.wards._Mansion.Rect";
            Mansion_Rect.prototype = {
                __class__: Mansion_Rect
            };
            var Market = function(a, b) {
                Ward.call(this, a, b)
            };
            g["com.watabou.mfcg.model.wards.Market"] = Market;
            Market.__name__ = "com.watabou.mfcg.model.wards.Market";
            Market.__super__ = Ward;
            Market.prototype = v(Ward.prototype, {
                createGeometry: function() {
                    C.restore(this.patch.seed);
                    this.space = this.getAvailable();
                    var a = .6;
                    null == a && (a = .5);
                    var b = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < a;
                    b ? a = !0 : (a = .3, null == a && (a = .5), a = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < a);
                    var c = null,
                        d = null;
                    if (b || a) d = PolyAccess.longest(this.space), c = this.space[d], d = this.space[(d + 1) % this.space.length];
                    if (b) {
                        b = this.monument = PolyCreate.rect(1 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647, 1 + (C.seed = 48271 * C.seed % 2147483647 |
                            0) / 2147483647);
                        var f = d.subtract(c);
                        f = Math.atan2(f.y, f.x);
                        PolyTransform.asRotateYX(b, Math.sin(f), Math.cos(f))
                    } else this.monument = PolyCreate.regular(8, 1 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647);
                    b = PolyCore.centroid(this.space);
                    a ? (a = GeomUtils.lerp(c, d), PolyTransform.asAdd(this.monument, GeomUtils.lerp(b, a, .2 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * .4))) : PolyTransform.asAdd(this.monument, b)
                },
                getAvailable: function() {
                    for (var a = this.patch.shape.length, b = [], c = 0, d = a; c < d;) c++, b.push(0);
                    var f = b;
                    b = [];
                    d = c = this.patch.face.halfEdge;
                    for (var h = !0; h;) {
                        var k = d;
                        d = d.next;
                        h = d != c;
                        b.push(k)
                    }
                    h = b;
                    b = [];
                    c = 0;
                    for (d = a; c < d;) {
                        a = c++;
                        k = h[a];
                        for (var n = 0, p = this.model.canals; n < p.length;) {
                            var g = p[n];
                            ++n;
                            null != EdgeChain.edgeByOrigin(g.course, k.origin) && (f[a] = g.width / 2, k.origin == g.course[0].origin && (f[a] += 1.2))
                        }
                        b.push(k.data == Tc.CANAL ? this.model.getCanalWidth(k) / 2 : 0)
                    }
                    return Ward.inset(this.patch.shape, b, f)
                },
                __class__: Market
            });
            var Park = function(a, b) {
                Ward.call(this, a, b)
            };
            g["com.watabou.mfcg.model.wards.Park"] = Park;
            Park.__name__ = "com.watabou.mfcg.model.wards.Park";
            Park.__super__ = Ward;
            Park.prototype = v(Ward.prototype, {
                createGeometry: function() {
                    for (var a = this.getAvailable(), b = [], c = 0, d = a.length; c < d;) {
                        var f = c++,
                            h = a[f];
                        b.push(h);
                        b.push(GeomUtils.lerp(h, a[(f + 1) % a.length]))
                    }
                    this.green = Chaikin.render(b, !0, 3);
                    this.trees = null
                },
                spawnTrees: function() {
                    null == this.trees && (this.trees = Forester.fillArea(this.getAvailable(), this.patch.district.greenery));
                    return this.trees
                },
                __class__: Park
            });
            var WardGroup = function(a) {
                this.faces = a;
                for (var b = 0; b < a.length;) {
                    var c = a[b];
                    ++b;
                    va.__cast(c.data.ward, Alleys).group = this
                }
                this.core = a[0];
                this.model = this.core.data.ward.model;
                this.district = this.core.data.district;
                if (1 == a.length) {
                    this.shape = this.core.data.shape;
                    b = [];
                    for (var d = c = this.core.halfEdge, f = !0; f;) a = d, d = d.next, f = d != c, b.push(a);
                    this.border = b
                } else this.border = a.length < this.district.faces.length ? DCEL.circumference(null, a) : this.district.border, this.shape = EdgeChain.toPoly(this.border);
                this.inner = [];
                this.blockM = new pa;
                b = 0;
                for (c = this.border; b < c.length;) a = c[b], ++b, d = a.origin, a.face.data.withinWalls || this.isInnerVertex(d) ? (this.inner.push(d), this.blockM.set(d.point, 1)) : this.blockM.set(d.point,
                    9);
                this.urban = this.inner.length == this.border.length
            };
            g["com.watabou.mfcg.model.wards.WardGroup"] = WardGroup;
            WardGroup.__name__ = "com.watabou.mfcg.model.wards.WardGroup";
            WardGroup.getCircle = function(a, b, c, d) {
                c = GeomUtils.intersectLines(a.x, a.y, -b.y, b.x, c.x, c.y, -d.y, d.x);
                a = new I(a.x - b.y * c.x, a.y + b.x * c.x);
                b = b.get_length() * c.x;
                return new Circle(a, b)
            };
            WardGroup.getArc = function(a, b, c, d) {
                b - c > Math.PI ? b -= 2 * Math.PI : c - b > Math.PI && (c -= 2 * Math.PI);
                var f = Math.abs(a.r);
                d = Math.abs(b - c) * f / d | 0;
                if (2 < d) {
                    for (var h = [], k = 0; k < d;) {
                        var n = k++/(d-1);null==n&&(n=.5);h.push(a.c.add(I.polar(f,
                        b + (c - b) * n)))
        }
        return h
    }
    return null
};
WardGroup.prototype = {
    createGeometry: function() {
        C.restore(this.core.data.seed);
        var a = this.getAvailable();
        if (null == a) hb.trace("Failed to calculate the available area", {
            fileName: "Source/com/watabou/mfcg/model/wards/WardGroup.hx",
            lineNumber: 83,
            className: "com.watabou.mfcg.model.wards.WardGroup",
            methodName: "createGeometry"
        }), this.alleys = [], this.blocks = [];
        else {
            var b = 20;
            do {
                this.blocks = [];
                this.alleys = [];
                this.church = null;
                var c = a.slice(),
                    d = PolyCore.area(c),
                    f = this.district.alleys;
                d > f.minSq *
                    Math.pow(2, f.sizeChaos * (2 * ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) - 1)) * f.blockSize ? this.createAlleys(c) : this.createBlock(c);
                this.urban || this.filter()
            } while (0 == this.blocks.length && 0 < b--)
        }
        0 == this.blocks.length && hb.trace("Failed to create a non-empty alleys group", {
            fileName: "Source/com/watabou/mfcg/model/wards/WardGroup.hx",
            lineNumber: 107,
            className: "com.watabou.mfcg.model.wards.WardGroup",
            methodName: "createGeometry"
        })
    },
    getAvailable: function() {
        for (var a = this.shape.length, b = [], c = 0, d = this.border; c <
            d.length;) {
            var f = d[c];
            ++c;
            for (var h = 0, k = 0, n = this.model.walls; k < n.length;) {
                var p = n[k];
                ++k;
                p = p.getTowerRadius(f.origin);
                0 < p && (p += 1.2);
                h = Math.max(h, p)
            }
            b.push(h)
        }
        c = [];
        d = 0;
        for (k = a; d < k;) {
            a = d++;
            f = this.border[a];
            n = 0;
            for (h = this.model.canals; n < h.length;) p = h[n], ++n, null != EdgeChain.edgeByOrigin(p.course, f.origin) && (b[a] = p.width / 2 + 1.2, f.origin == p.course[0].origin && (b[a] += 1.2));
            if (null == f.data) c.push(f.twin.face.data == this.model.plaza ? 1 : .6);
            else {
                switch (f.data._hx_index) {
                    case 1:
                        n = f.face.data.landing ? 2 : 1.2;
                        break;
                    case 2:
                        n =
                            1;
                        break;
                    case 3:
                        n = CurtainWall.THICKNESS / 2 + 1.2;
                        break;
                    case 4:
                        n = this.model.getCanalWidth(f) / 2 + 1.2;
                        break;
                    default:
                        n = 0
                }
                c.push(n)
            }
        }
        return Ward.inset(this.shape, c, b)
    },
    createAlleys: function(a) {
        var b = this.district.alleys,
            c = new Bisector(a, b.minSq * b.blockSize, 16 * this.district.alleys.gridChaos);
        c.getGap = function(a) {
            return 1.2
        };
        c.processCut = l(this, this.semiSmooth);
        this.urban || (c.isAtomic = l(this, this.isBlockSized));
        a = 0;
        for (var d = c.partition(); a < d.length;) {
            var f = d[a];
            ++a;
            var h = PolyCore.area(f);
            b = this.district.alleys;
            b = b.minSq * Math.pow(2,
                b.sizeChaos * (2 * ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) - 1));
            h < b ? this.createBlock(f, !0) : null == this.church && h <= 4 * b ? this.createChurch(f) : this.createBlock(f)
        }
        b = this.alleys;
        c = c.cuts;
        for (a = 0; a < c.length;) d = c[a], ++a, b.push(d)
    },
    semiSmooth: function(a) {
        var b = a[0],
            c = a[1],
            d = a[2],
            f = I.distance(b, d),
            h = Math.abs(PolyCore.area(a));
        if (1 > h / f || .01 > h / (f * f)) return [b, d];
        h = c.subtract(b);
        var k = d.subtract(c),
            n = h.get_length(),
            p = k.get_length(),
            g = Math.min(n, p);
        f = this.district.alleys.minFront;
        var q = (1 - (h.x * k.x + h.y * k.y) / n / p) / 2;
        null == q && (q = .5);
        (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < q ? g = !0 : (q = f / g, null == q && (q = .5), g = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < q);
        if (g) return a;
        n < p ? (n /= p, c = new I(c.x + k.x * n, c.y + k.y * n), h = WardGroup.getCircle(b, h, c, k), n = b.subtract(h.c), k = Math.atan2(n.y, n.x), n = c.subtract(h.c), c = Math.atan2(n.y, n.x), f = WardGroup.getArc(h, k, c, f), null != f ? f.push(d) : f = a) : (n = -p / n, c = new I(c.x + h.x * n, c.y + h.y * n), h = WardGroup.getCircle(c, h, d, k), n = c.subtract(h.c), k = Math.atan2(n.y, n.x), n = d.subtract(h.c), c = Math.atan2(n.y, n.x), f = WardGroup.getArc(h, k,
            c, f), null != f ? f.unshift(b) : f = a);
        return f
    },
    createBlock: function(a, b) {
        null == b && (b = !1);
        a = new Block(this, a, b);
        0 < a.lots.length && this.blocks.push(a)
    },
    createChurch: function(a) {
        var b = PolyBounds.obb(a),
            c = b[0].subtract(b[1]),
            d = b[2].subtract(b[1]);
        c = c.get_length() > d.get_length() ? c : d;
        d = this.district.alleys.minFront / c.get_length();
        d = .5 < d ? .5 : d + (1 - 2 * d) * (((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3);
        b = b[1];
        b = new I(b.x + c.x * d, b.y +
            c.y * d);
        c = b.add(new I(-c.y, c.x));
        a = PolyCut.cut(a, b, c);
        a = Z.max(a, function(a) {
            return PolyCore.compactness(a)
        });
        this.church = new Block(this, a, !0);
        this.blocks.push(this.church)
    },
    filter: function() {
        var a = new pa,
            b = this.border,
            c = b[b.length - 1],
            d = 0;
        for (b = this.border; d < b.length;) {
            var f = b[d];
            ++d;
            var h = f.origin,
                k = h.point;
            if (-1 != this.inner.indexOf(h)) var n = 1;
            else {
                var p = [];
                h = c;
                if (null == h.data) h = 0;
                else switch (h.data._hx_index) {
                    case 2:
                        h = .3;
                        break;
                    case 3:
                        h = .5;
                        break;
                    case 4:
                        h = .1;
                        break;
                    default:
                        h = 0
                }
                p.push(h);
                h = f;
                if (null == h.data) h = 0;
                else switch (h.data._hx_index) {
                    case 2:
                        h =
                            .3;
                        break;
                    case 3:
                        h = .5;
                        break;
                    case 4:
                        h = .1;
                        break;
                    default:
                        h = 0
                }
                p.push(h);
                n = Math.max(p[0], p[1])
            }
            a.set(k, n);
            c = f
        }
        f = Math.sqrt(this.faces.length);
        k = .5 * f - .5;
        d = 0;
        for (b = this.blocks; d < b.length;) {
            c = b[d];
            ++d;
            p = [];
            for (var g = 0, q = c.lots; g < q.length;) h = q[g], ++g, n = PolyCore.center(h), n = this.interpolate(n, a), isNaN(n) ? n = !1 : (n = n * f - k, null == n && (n = .5), n = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < n), n && p.push(h);
            c.lots = p
        }
        d = [];
        b = 0;
        for (p = this.blocks; b < p.length;) h = p[b], ++b, 0 < h.lots.length && d.push(h);
        a = d;
        b = this.blocks;
        b.splice(0, b.length);
        b = this.blocks;
        for (d = 0; d < a.length;) h = a[d], ++d, b.push(h)
    },
    getTris: function() {
        if (null == this.tris) {
            for (var a = [], b = 0, c = Triangulation.earcut(this.shape); b < c.length;) {
                var d = c[b];
                ++b;
                a.push([this.shape[d[0]], this.shape[d[1]], this.shape[d[2]]])
            }
            this.tris = a
        }
        return this.tris
    },
    interpolate: function(a, b) {
        for (var c = 0, d = this.getTris(); c < d.length;) {
            var f = d[c];
            ++c;
            var h = GeomUtils.barycentric(f[0], f[1], f[2], a);
            if (0 <= h.x && 0 <= h.y && 0 <= h.z) return h.x * b.h[f[0].__id__] + h.y * b.h[f[1].__id__] + h.z * b.h[f[2].__id__]
        }
        return NaN
    },
    isInnerVertex: function(a) {
        return Z.every(a.edges,
            function(a) {
                a = a.face.data;
                return a.withinCity ? !0 : a.waterbody
            })
    },
    isBlockSized: function(a) {
        var b = PolyCore.center(a);
        a = PolyCore.area(a);
        var c = this.district.alleys;
        b = c.minSq * c.blockSize * this.interpolate(b, this.blockM);
        return a < b
    },
    __class__: WardGroup
};
var Wilderness = function(a, b) {
    Ward.call(this, a, b)
};
g["com.watabou.mfcg.model.wards.Wilderness"] = Wilderness;
Wilderness.__name__ = "com.watabou.mfcg.model.wards.Wilderness";
Wilderness.__super__ = Ward;
Wilderness.prototype = v(Ward.prototype, {
    __class__: Wilderness
});
var TownScene = function() {
    Scene.call(this);
    TownScene.inst = this;
    this.model = City.instance;
    this.createOverlays();
    this.updateOverlays();
    this.toggleOverlays();
    Main.preview || this.keyEvent.add(l(this, this.onKeyEvent))
};
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
