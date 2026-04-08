from battle.map import Map
import time
import sys, os

if os.name != 'nt':
    import termios
    import tty
from collections import deque
from random import randint
from numpy import mean

from ia.registry import AI_REGISTRY

def fix_string(string):
        """Transforme une chaîne de caractères en une version "fixe" (minuscules, sans espaces ou caractères spéciaux)"""
        str_void = ""
        bad_chars = [' ', '-', '_', '.', ',', ';', ':', '!', '?', '/', '\\', '|', '@', '#', '$', '%', '^', '&', '*', '(', ')', '[', ']', '{', '}', '<', '>', '~', '`', '"', "'"]
        for char in string:
            if char in bad_chars:
                continue
            str_void += char.lower()
        return str_void

class Engine:
    def __init__(self, scenario, ia, view_type):
        self.scenario_name = scenario
        self.ia1 = fix_string(ia)

        self.game_map = None
        self.units = []
        self.projectiles = []
        self.game_pause = False
        self.current_turn = 0
        self.is_running = False
        self.winner = None
        self.view = None
        self.pressed_keys = set()
        self.real_tps = 0
        # Historique des t/s des dernières turns (max 10)
        self.tab_game_tps = deque(maxlen=10)
        self.tab_tps_affichage = deque(maxlen=120)

        self.star_execution_time = None
        # Nouvelles stats tournois
        self.ia_thinking_time = {'R': 0.0, 'B': 0.0}
        self.initial_units_count = {'R': 0, 'B': 0}
        self.history = {'turns': [], 'red_units': [], 'blue_units': []}

        # Vue
        self.view_type = view_type
        # frame rate controles
        self.max_fps = 60  # <-- FPS MAX pas besoin de plus ca fait trop de fluctuation sinon
        self.min_fps = 10  # <-- FPS MIN
        self.min_frame_delay = 1 / self.max_fps 
        self.max_frame_delay = 1 / self.min_fps
        # tick rate / limit
        self.tps = 60  # <-- target TPS: Vitesse du jeu [= 60 pour un time scale =1 ] /!\ la moyenne reste toujours sous cette valeur... /!\
        self.turn_time_target = 1.0 / self.tps  # en secondes
        self.star_execution_time = None
        self.turn_time = 0

        self.max_turns = 40000
        self.turn_fps = 0
        self.time_turn = 0
        self.units = []
        pass
     
    def game_loop(self):
        """Boucle principale du jeu"""

        view_frame_time = max(1 / 100, 2 / (self.max_fps + self.min_fps))  # <-- 1/FPS au demarrage
        self.turn_time_target = 1.0 / self.tps  # en secondes
        max_turn_time = self.turn_time_target

        next_view_time = time.time()

        while self.is_running and self.current_turn < self.max_turns:
            turn_start = time.time()
            if not self.game_pause:
                ######################################################
                #####             - FPS throttling -          ########
                #####      " C'est moche mais ca marche "     ########
                ######################################################
                # FPS jamais au dessus de  TPS
                # FPS jamais au dessus de  max_fps
                # FPS jamais en dessous de min_fps, sauf si TPS < min_fps
                if self.view_type > 1 and self.current_turn % 5 == 0:

                    if self.real_tps == 0: tps =60 
                    else: tps = self.real_tps
                    if self.tps <= 0: 
                        self.tps =0
                        perf =1
                    else: perf = tps / (self.tps)  # stabilise autour de tps cible
                    
                    #self.turn_time_target = 1.0 / max(self.tps,1)
                    #print(perf)

                    view_frame_time= max(min(( view_frame_time / perf), self.max_frame_delay), self.min_frame_delay)

                    self.turn_time_target = max(min(( self.turn_time_target * perf), 1/(self.tps+3)), 1/(self.tps+30))
                    
                    
                    view_frame_time =max( 1/tps , view_frame_time)   #fps jamais > tps
                    self.turn_fps = 1 / view_frame_time
                    max_turn_time = self.turn_time_target 

                    #print(1/max_turn_time)
                    ##################################################################

                self.process_turn()
                # 1. Gérer les entrées
                self.handle_input()
                # 2. Mettre à jour l'affichage
                if turn_start >= next_view_time and self.view_type > 0:
                    next_view_time = turn_start + view_frame_time
                    self.update_view()
                # 3. Vérifier les conditions de victoire
                self.check_victory()
                # 4. Passer au tour suivant
                self.current_turn += 1
                # 5 mets a jour les unités
                self.update_units(1 / 60)
                self.update_projectiles()
                # 5. Contrôle du turn rate
                self.turn_time = time.time() - turn_start
                if self.view and self.turn_time < max_turn_time:
                    time.sleep(max_turn_time - self.turn_time)
                turn_time_plusp = time.time() - turn_start
                if turn_time_plusp != 0:
                    self.tab_game_tps.append((1.0 / turn_time_plusp))
                    self.tab_tps_affichage.append(1.0 / turn_time_plusp)
                self.real_tps = (sum(self.tab_game_tps) / len(self.tab_game_tps)) if self.tab_game_tps else 0
                self.time_turn = time.time()