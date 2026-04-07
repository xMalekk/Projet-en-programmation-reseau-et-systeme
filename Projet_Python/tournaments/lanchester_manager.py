import re
import os
from battle.engine import Engine
from battle.scenario import Scenario
from reports.reporter import generate_report


class LanchesterPlotManager:
    def __init__(self, ia1, ia2, scenario_base, units_str, range_str, out_file="lanchester_plot_report.html"):
        self.ia1 = ia1
        self.ia2 = ia2
        self.scenario_base = scenario_base
        self.out_file = out_file

        self.units = self._parse_units(units_str)
        self.range = self._parse_range(range_str)

        self.results = []
        self.run_simulations()

    def _parse_units(self, s):
        # Enlever les crochets et espaces
        s = s.replace("[", "").replace("]", "").strip()
        parts = [p.strip().lower() for p in s.split(",")]

        mapping = {
            "knight": "K", "k": "K",
            "crossbow": "C", "crossbowman": "C", "c": "C",
            "spearman": "P", "pikeman": "P", "p": "P",
            "longsword": "L", "l": "L",
            "skirmisher": "S", "s": "S"
        }

        res = [mapping.get(p, p.upper()) for p in parts if p]
        if not res:
            return "K", "K"
        if len(res) == 1:
            return res[0], res[0]
        return res[0], res[1]

    def _parse_range(self, s):
        # Match range(start, stop[, step])
        match = re.match(r"range\((\d+),\s*(\d+)(?:,\s*(\d+))?\)", s)
        if match:
            start = int(match.group(1))
            stop = int(match.group(2))
            step = int(match.group(3)) if match.group(3) else 1
            return range(start, stop, step)

        # Fallback si format invalide
        return range(10, 110, 20)

    def run_simulations(self):
        unit_red, unit_blue = self.units
        # On fixe Red à la valeur max du range pour avoir une base de comparaison
        fixed_red = list(self.range)[-1]

        scenario_tool = Scenario()

        print(f"=== Starting Lanchester Plot Simulations ===")
        print(f"Red: {self.ia1} ({unit_red}) x {fixed_red}")
        print(f"Blue: {self.ia2} ({unit_blue}) x {list(self.range)}")

        for n_blue in self.range:
            scen_name = f"{self.scenario_base}_{n_blue}"
            # On génère le scénario
            scenario_tool.create_lanchester_scenario_N(scen_name, (200, 100), unit_red, unit_blue, fixed_red, n_blue)

            # On lance le moteur (sans terminal, mode tournoi pour aller vite)
            engine = Engine(f"{scen_name}_lanchester", self.ia1, self.ia2, view_type=0, tournaments=True)
            res = engine.start()

            self.results.append({
                "n_red_initial": fixed_red,
                "n_blue_initial": n_blue,
                "n_red_final": res['units_ia1'],
                "n_blue_final": res['units_ia2'],
                "winner": res['winner_ia'],
                "turns": res['turn']
            })
            print(
                f"Match {fixed_red} vs {n_blue} -> Final: {res['units_ia1']} vs {res['units_ia2']} ({res['winner_ia']})")

        self.generate_plot_report()

    def generate_plot_report(self):
        data = {
            "ia1": self.ia1,
            "ia2": self.ia2,
            "unit_red": self.units[0],
            "unit_blue": self.units[1],
            "results": self.results
        }
        generate_report('lanchester_plot', data, self.out_file)
        print(f"=== Plot simulations finished. Report generated: {self.out_file} ===")
