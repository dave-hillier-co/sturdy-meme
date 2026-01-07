/*
 * mfcg-model/01-utils-annotations.js
 * Part 1/8: Utilities and Annotations
 * Contains: Buffer, Values, Equator, Ridge
 */
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
