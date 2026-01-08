/*
 * mfcg-model/03-linguistics.js
 * Part 3/8: Linguistics
 * Contains: Namer (town/street name generation)
 */
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
