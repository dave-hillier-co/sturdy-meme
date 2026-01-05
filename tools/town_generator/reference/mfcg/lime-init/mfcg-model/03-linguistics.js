/*
 * mfcg-model/03-linguistics.js
 * Part 3/8: Linguistics
 * Contains: Namer (town/street name generation)
 */
            g["com.watabou.mfcg.linguistics.Namer"] = Ma;
            Ma.__name__ = "com.watabou.mfcg.linguistics.Namer";
            Ma.reset = function() {
                if (null == Ma.grammar) {
                    Oe.rng = C.float;
                    Ma.grammar = new hk(JSON.parse(ac.getText("grammar")));
                    Ma.grammar.defaultSelector = $h;
                    Ma.grammar.addModifiers(jb.get());
                    var a = Ma.grammar,
                        b = new Qa;
                    b.h.merge = Ma.merge;
                    a.addModifiers(b);
                    Ma.grammar.addExternal("word", Ma.word);
                    Ma.grammar.addExternal("fantasy", Ma.fantasy);
                    Ma.grammar.addExternal("given",
                        Ma.given);
                    Ma.english = new de(ac.getText("english").split(" "));
                    Ma.elven = new de(ac.getText("elven").split(" "));
                    Ma.male = new de(ac.getText("male").split(" "));
                    Ma.female = new de(ac.getText("female").split(" "))
                }
                Ma.grammar.clearState()
            };
            Ma.given = function() {
                return (.5 > (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 ? Ma.male : Ma.female).generate(4)
            };
            Ma.word = function() {
                return Ma.generate(Ma.english, 4, 3)
            };
            Ma.fantasy = function() {
                return Ma.generate(Ma.elven)
            };
            Ma.generate = function(a, b, c) {
                null == c && (c = 4);
                for (null == b &&
                    (b = 1);;) {
                    var d = a.generate(c);
                    if (d.length >= b && -1 == a.source.indexOf(d)) return d
                }
            };
            Ma.cityName = function(a) {
                var b = Ma.grammar,
                    c = a.bp.citadel;
                null == c && (c = !0);
                c ? Z.addAll(b.flags, (new ja(", +", "")).split("CASTLE")) : Z.removeAll(b.flags, (new ja(", +", "")).split("CASTLE"));
                b = Ma.grammar;
                c = a.bp.walls;
                null == c && (c = !0);
                c ? Z.addAll(b.flags, (new ja(", +", "")).split("WALLS")) : Z.removeAll(b.flags, (new ja(", +", "")).split("WALLS"));
                b = Ma.grammar;
                c = a.bp.temple;
                null == c && (c = !0);
                c ? Z.addAll(b.flags, (new ja(", +", "")).split("TEMPLE")) :
                    Z.removeAll(b.flags, (new ja(", +", "")).split("TEMPLE"));
                b = Ma.grammar;
                c = a.bp.plaza;
                null == c && (c = !0);
                c ? Z.addAll(b.flags, (new ja(", +", "")).split("PLAZA")) : Z.removeAll(b.flags, (new ja(", +", "")).split("PLAZA"));
                b = Ma.grammar;
                c = a.bp.shanty;
                null == c && (c = !0);
                c ? Z.addAll(b.flags, (new ja(", +", "")).split("SHANTY")) : Z.removeAll(b.flags, (new ja(", +", "")).split("SHANTY"));
                b = Ma.grammar;
                c = a.bp.coast;
                null == c && (c = !0);
                c ? Z.addAll(b.flags, (new ja(", +", "")).split("COAST")) : Z.removeAll(b.flags, (new ja(", +", "")).split("COAST"));
                b = Ma.grammar;
                c = a.bp.river;
                null == c && (c = !0);
                c ? Z.addAll(b.flags, (new ja(", +", "")).split("RIVER")) : Z.removeAll(b.flags, (new ja(", +", "")).split("RIVER"));
                return eh.capitalizeAll(Ma.grammar.flatten("#city#"))
            };
            Ma.nameDistricts = function(a) {
                if (1 == a.districts.length) a.districts[0].name = a.name;
                else {
                    var b = 1 == a.name.split(" ").length ? a.name : "city";
                    Ma.grammar.pushRules("cityName", [null != b ? b : Ma.grammar.flatten("#cityName#")]);
                    for (var c = 0, d = a.districts; c < d.length;) {
                        var f = [d[c]];
                        ++c;
                        b = Ma.getDistrictNoun(f[0]);
                        Ma.grammar.pushRules("districtNoun", [null != b ? b : Ma.grammar.flatten("#districtNoun#")]);
                        b = Ma.getDirection(f[0]);
                        Ma.grammar.pushRules("dir", [null != b ? b : Ma.grammar.flatten("#dir#")]);
                        b = Ma.grammar;
                        var h = 1 == f[0].faces.length;
                        null == h && (h = !0);
                        h ? Z.addAll(b.flags, (new ja(", +", "")).split("COMPACT")) : Z.removeAll(b.flags, (new ja(", +", "")).split("COMPACT"));
                        b = Ma.grammar;
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
                                b = Ma.grammar.flatten("#centralDistrict#");
                                break;
                            case 1:
                                b = Ma.grammar.flatten("#castleDistrict#");
                                break;
                            case 2:
                                b = Ma.grammar.flatten("#docksDistrict#");
                                break;
                            case 3:
                                b = Ma.grammar.flatten("#bridgeDistrict#");
                                break;
                            case 4:
                                b = Ma.grammar.flatten("#gateDistrict#");
                                break;
                            case 5:
                                b = Ma.grammar.flatten("#bankDistrict#");
                                break;
                            case 6:
                                b = Ma.grammar.flatten("#parkDistrict#");
                                break;
                            case 7:
                                b = Ma.grammar.flatten("#sprawlDistrict#");
                                break;
                            default:
                                b = Ma.grammar.flatten("#district#")
                        }
                        Ma.grammar.popRules("dir");
                        Ma.grammar.popRules("districtNoun");
                        f[0].name = eh.capitalizeAll(b)
                    }
                    Ma.grammar.popRules("cityName")
                }
            };
            Ma.getDistrictNoun = function(a) {
                a = a.faces.length + Math.floor((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * 3);
                return 2 >= a ? "quarter" : 6 > a ? "ward" : 12 > a ? "district" : "town"
            };
            Ma.getDirection = function(a) {
                var b = null,
                    c = -Infinity;
                null == a.equator && (a.equator = ze.build(Ua.toPoly(a.border)));
                a = qa.lerp(a.equator[0], a.equator[1]);
                for (var d = Ma.dirs,
                        f = d.keys(); f.hasNext();) {
                    var h = f.next(),
                        k = d.get(h);
                    h = h.x * a.x + h.y * a.y;
                    c < h && (c = h, b = k)
                }
                return Ma.grammar.flatten("#" + b + "#")
            };
            Ma.merge = function(a, b) {
                b = a.split(" ");
                if (1 == b.length) return a;
                for (var c = 0, d = 0; d < b.length;) {
                    var f = b[d];
                    ++d;
                    c += nd.split(f).length
                }
                return 2 >= c ? b.join("") : a
            };
            var hd = function() {};
