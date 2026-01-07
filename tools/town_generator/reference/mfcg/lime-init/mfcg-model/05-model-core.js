/*
 * mfcg-model/05-model-core.js
 * Part 5/8: Core Model Classes
 * Contains: Blueprint, Building, Canal, Cell, City, District, Grower, etc.
 */
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
