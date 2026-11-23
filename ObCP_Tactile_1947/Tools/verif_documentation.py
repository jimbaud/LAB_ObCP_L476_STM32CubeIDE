#!/usr/bin/env python3
"""Contrôler la couverture des commentaires applicatifs dans le XML Doxygen.

Exécuter doxygen Doxyfile, puis python Tools/verif_documentation.py. Le contrôle
porte sur les fichiers utilisateur de Core/Src/obcp et leurs interfaces. Il compte
les descriptions, paramètres, retours et champs, sans juger leur qualité pédagogique.
Les bibliothèques ST et le banc de tests ne font pas partie de cette API extraite.
"""
import re
import sys
from collections import Counter
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
XML = ROOT / "build/documentation/xml"


def content(element):
    """Récupérer le texte utile d’un nœud XML, ou une chaîne vide s’il est absent."""
    return "" if element is None else " ".join("".join(element.itertext()).split())


def definitions():
    """Lister les fonctions définies dans les sources applicatives actuelles.

    Le motif est limité aux signatures C du projet. Chaînes et commentaires sont
    masqués pour ignorer leurs exemples. Doxygen fournit l’analyse des contrats.
    """
    functions = set()
    pattern = re.compile(
        r"(?m)^(?:static\s+)?(?:const\s+)?[A-Za-z_]\w*"
        r"(?:[ \t]+[A-Za-z_]\w*)*[ \t*]+([A-Za-z_]\w*)\s*\([^;{}]*?\)\s*\{"
    )
    for path in (ROOT / "Core/Src/obcp").glob("*.c"):
        text = path.read_text(encoding="utf-8")
        text = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', " ", text, flags=re.S)
        functions.update((path.name, match[1]) for match in pattern.finditer(text))
    return functions


def main():
    """Comparer les définitions C aux contrats XML et retourner zéro au succès."""
    if not (XML / "index.xml").exists():
        print("Exécuter d’abord doxygen Doxyfile.")
        return 1
    warnings = ROOT / "build/documentation_warnings.txt"
    issues = []
    if not warnings.exists() or warnings.read_text(encoding="utf-8").strip():
        issues.append("Journal Doxygen absent ou contenant des avertissements.")
    found, seen, interfaces = set(), set(), set()
    counts = Counter()
    index = ET.parse(XML / "index.xml").getroot()
    for compound in index.findall("compound"):
        # Les groupes répètent les membres des fichiers. Les ignorer évite un double compte.
        if compound.get("kind") not in ("file", "struct"):
            continue
        root = ET.parse(XML / (compound.get("refid") + ".xml")).getroot()
        for member in root.findall(".//memberdef"):
            identifier = member.get("id")
            if identifier in seen:
                continue
            seen.add(identifier)
            kind, name = member.get("kind"), member.findtext("name", "")
            location = member.find("location")
            if location is None:
                continue
            description = content(member.find("briefdescription")) + content(member.find("detaileddescription"))
            if not description:
                issues.append(f"Description absente pour {name}")
            if kind == "function":
                body = location.get("bodyfile", "")
                if not body.startswith("Core/Src/obcp/"):
                    continue
                key = (Path(body).name, name)
                declaration = location.get("declfile", location.get("file", ""))
                if declaration.endswith(".h"):
                    interfaces.add(key)
                # Une même fonction publique apparaît dans son .h et son .c avec deux identifiants XML.
                if key in found:
                    continue
                found.add(key)
                counts["fonctions"] += 1
                counts["publiques" if member.get("static") == "no" else "privees"] += 1
                if not content(member.find("briefdescription")):
                    issues.append(f"Résumé @brief absent pour {name}")
                declared = {content(p.find("declname")) for p in member.findall("param")
                            if content(p.find("type")) != "void"}
                documented = {content(p) for p in member.findall(".//parameterlist[@kind='param']//parametername")}
                if declared != documented:
                    issues.append(f"Paramètres de {name} : déclarés {sorted(declared)}, documentés {sorted(documented)}")
                counts["parametres"] += len(declared)
                if content(member.find("type")) != "void" and not member.findall(".//simplesect[@kind='return']"):
                    issues.append(f"Retour @return absent pour {name}")
            elif compound.get("kind") == "struct" and kind == "variable":
                counts["champs"] += 1
            elif kind in ("enum", "define", "variable", "typedef"):
                counts[kind] += 1
            if kind == "enum":
                for value in member.findall("enumvalue"):
                    counts["valeurs_enum"] += 1
                    if not content(value.find("briefdescription")) + content(value.find("detaileddescription")):
                        issues.append("Constante non documentée : " + value.findtext("name", ""))
    expected = definitions()
    for filename, name in sorted(expected - found):
        issues.append(f"Fonction du code absente du XML : {filename}/{name}")
    for filename, name in sorted(found - expected):
        issues.append(f"Fonction XML absente du code actuel : {filename}/{name}")
    counts["interfaces"] = len(interfaces)
    print("Couverture des modules utilisateur, Doxygen XML")
    for label, count in sorted(counts.items()):
        print(f"{label} : {count}")
    for issue in issues:
        print("ÉCART", issue)
    print(f"Bilan {len(issues)} écart(s). Ce contrôle ne constitue pas une certification de code.")
    return int(bool(issues))


if __name__ == "__main__":
    sys.exit(main())
