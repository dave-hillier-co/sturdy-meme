/*
 * mfcg-model/07-wards.js
 * Part 7/8: Ward Types
 * Contains: Ward, Alleys, Castle, Cathedral, Farm, Harbour, Mansion, etc.
 */
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
