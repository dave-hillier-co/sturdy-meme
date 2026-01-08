/*
 * mfcg-model/06-blocks.js
 * Part 6/8: Block Types
 * Contains: Block, TwistedBlock
 */
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
