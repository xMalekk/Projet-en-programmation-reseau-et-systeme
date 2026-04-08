"""
Point d'entrée principal du jeu
"""

import argparse
import sys
import os

from battle.engine2 import Engine
from battle.scenario import Scenario
from ia.registry import AI_REGISTRY
global tps

if not os.path.exists("data/scenario"):
    os.mkdir("data/scenario")

def help():
    print("Utilisation : battle <commande> [options]")
    print("battle create <scenario> <ia> / Crée une carte avec une armée")
    print("battle join <IP> <ia> / Rejoins une partie")
    print("")
    
    print("Liste des scénarios disponibles :")
    scenarios = Scenario().list_scenarios()
    print(" Scénarios :")
    for s in scenarios:
        print(f"  - {s}")
    print("")
    
    print("Liste des IA disponibles :")
    for key in AI_REGISTRY.keys():
        print(f" - {key}")
    print("")
    
    # print("Exemple de commandes :")
    # print("python3 main.py battle run stest6 smartia  Major_DAFT")
    # print("python3 main.py battle tournament")
    # print("python3 main.py battle load stest1 (ou stest1_save)")

class BattleCLI:
    def __init__(self):
        parser = argparse.ArgumentParser(
            prog="battle",
            description="Battle simulation CLI — create or join a game."
        )
        subparsers = parser.add_subparsers(dest="command", required=True)

        # === battle create <scenario> <ia> ===
        create_parser = subparsers.add_parser("create", help="Create a scenario with 1 ia.")
        create_parser.add_argument("scenario", help="Scenario name or file to use")
        create_parser.add_argument("ia", help="Name of ia")

        # === battle join <IP> <ia> ===
        join_parser = subparsers.add_parser("join", help="Join a game with 1 ia.")
        join_parser.add_argument("IP", help="IP of the game to join")
        join_parser.add_argument("ia", help="Name of ia")
        
        self.parser = parser


    ### === Command dispatch ===
    def run(self):
        """pour faire vos tests complets depuis la ligne de commande initiale,
        il vous suffit de modifier ce tableau dans le fichier mian,
        il agira comme si vous aviez tapper la ligne de commande qui est dedans"""

        if len(sys.argv) < 2:
            help()
            return

        else:
            sys.argv.pop(0)  # Retire le nom du script

        if sys.argv[1] == "create":
            scenario_path = f"data/scenario/{sys.argv[2]}.txt"
            if not os.path.exists(scenario_path):
                return print(f"Le scénario {sys.argv[2]} n'existe pas.")

        args = self.parser.parse_args()
        match args.command:
            case "create":
                self.cmd_create(args)
            case "join":
                self.cmd_join(args)

    # === Command implementations  ===
    def cmd_create(self, args):

        print(f"[CREATE] Scenario: {args.scenario}")
        print(f"      ia: {args.ia}")
        view_type = 2
        engine = Engine(args.scenario, args.ia, view_type)
        engine.start()
    
    def cmd_join(self, args):

        print(f"[JOIN] IP: {args.IP}")
        print(f"      ia: {args.ia}")
        view_type = 2
        engine = Engine(args.IP, args.ia, view_type)
        engine.start()


if __name__ == "__main__":
    if len(sys.argv) == 1:
        pass

        # sys.argv = [".","battle", "run", "stest2", "Major_DAFT", "Major_DAFT", "--no-terminal"]
        # sys.argv = [".","battle", "run", "stest2_lanchester", "Major_DAFT", "Major_DAFT"]
        # sys.argv = [".","battle", "run", "stest1_save", "Major_DAFT", "Major_DAFT"]
        # sys.argv = [".","battle", "tournament"]
        # sys.argv = [".", "battle", "run", "stest1", "basicia", "Major_DAFT"]
        # sys.argv = [".","battle", "load", "autosave"]

    BattleCLI().run()
