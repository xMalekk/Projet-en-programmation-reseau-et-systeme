from battle.map import Map
import time
import visuals.gui_view as gui
from collections import deque
from random import randint
from numpy import mean
from network.bridge import NetworkBridge

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
    def __init__(self, scenario, ia, ipc_port):
        self.bridge = NetworkBridge(ipc_port=ipc_port)
        self.bridge.connect()
        if not scenario:
            self.bridge.send_event("JOIN")
            while True:
                time.sleep(2)
                message = self.bridge.receive_event()
                if message and message[0] == "ACCEPT":
                    self.nbr_joueurs = int(message[1])
                    self.scenario_name = message[2]
                    break
        else:
            self.nbr_joueurs = 1
            self.scenario_name = scenario
        self.team = self.nbr_joueurs-1
        self.ia = fix_string(ia)
        self.game_map = Map(self.bridge, self.team)
        Map.load(self.game_map, self.scenario_name)

        self.projectiles = []
        self.game_pause = False
        self.current_turn = 0
        self.is_running = False
        self.winner = None
        self.view = gui.GUI_view(self.game_map.p, self.game_map.q)
        self.units = []
        for pos in self.game_map.map:
            self.units.append(self.game_map.map[pos])
            self.view.all_units.append(self.game_map.map[pos])
        self.real_tps = 0
        # Historique des t/s des dernières turns (max 10)
        self.tab_game_tps = deque(maxlen=10)
        self.tab_tps_affichage = deque(maxlen=120)

        self.star_execution_time = None
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
       
    def initialize_game(self):

        """donner IP pour celui qui rejoint, initialiser partie"""
        #self.bridge.connect()
    
    def initialize_ai(self):
        """Initialise les deux IA"""
        if self.ia not in AI_REGISTRY:
            raise ValueError(f"IA '{self.ia}' non reconnue.")

        self.ia = AI_REGISTRY[self.ia](self.team, self.game_map)

        self.ia.initialize()
    

    def game_loop(self):
        """Boucle principale du jeu"""
        self.initialize_ai()
        view_frame_time = max(1 / 100, 2 / (self.max_fps + self.min_fps))  # <-- 1/FPS au demarrage
        self.turn_time_target = 1.0 / self.tps  # en secondes
        max_turn_time = self.turn_time_target

        next_view_time = time.time()
        self.star_execution_time = time.time()

        while True:
            turn_start = time.time()
            if not self.game_pause:
                # FPS jamais au dessus de  TPS
                # FPS jamais au dessus de  max_fps
                # FPS jamais en dessous de min_fps, sauf si TPS < min_fps
                if self.current_turn+1 % 5 == 0:

                    if self.real_tps == 0: tps =60 
                    else: tps = self.real_tps
                    if self.tps <= 0: 
                        self.tps =0
                        perf = 1
                    else: perf = tps / (self.tps)  # stabilise autour de tps cible

                    view_frame_time= max(min(( view_frame_time / perf), self.max_frame_delay), self.min_frame_delay)

                    self.turn_time_target = max(min(( self.turn_time_target * perf), 1/(self.tps+3)), 1/(self.tps+30))
                     
                    view_frame_time =max( 1/tps , view_frame_time)   #fps jamais > tps
                    self.turn_fps = 1 / view_frame_time
                    max_turn_time = self.turn_time_target 

                    #print(1/max_turn_time)
                    ##################################################################

                self.process_turn()
                #  Mettre à jour l'affichage
                if turn_start >= next_view_time:
                    next_view_time = turn_start + view_frame_time
                    self.update_view()
                #  Passer au tour suivant
                self.current_turn += 1
                # Contrôle du turn rate
                self.turn_time = time.time() - turn_start
                if self.turn_time < max_turn_time:
                    time.sleep(max_turn_time - self.turn_time)
                turn_time_plusp = time.time() - turn_start
                if turn_time_plusp != 0:
                    self.tab_game_tps.append((1.0 / turn_time_plusp))
                    self.tab_tps_affichage.append(1.0 / turn_time_plusp)
                self.real_tps = (sum(self.tab_game_tps) / len(self.tab_game_tps)) if self.tab_game_tps else 0
                self.time_turn = time.time()

#######################WORK IN PROGRESS##########################################
    def process_turn(self):
        """Traite un tour de jeu (déplacements, combats, etc.)"""
        # recevoir les info de mouvements et attaques du distant, et autres
        self.update_units(1 / 60)
        self.update_projectiles()
        for unit in self.units:
            if not unit.is_alive:
                continue
            if unit.team == self.ia.team:
                self.ia.play_turn(unit, self.current_turn)
        
        while True:
            event = self.bridge.receive_event()
            if not event:
                break
            self.apply_ennemy_order(event)
            

##########################################################################

    def apply_ennemy_order(self, event):
        if event[0] == "UNIT_SPAWN":
            self.game_map.add_unit(event[4], event[5], event[1], event[3], event[2])
            self.units.append(self.game_map.get_unit(event[4], event[5]))
            self.view.all_units.append(self.game_map.get_unit(event[4], event[5]))
            self.ia.ack_unit(self.game_map.get_unit(event[4], event[5]))
        elif event[0] == "UNIT_MOVE":
            self.game_map.sync_remote_move(event[1], (event[2], event[3]))
        elif event[0] == "UNIT_ATTACK":
            self.game_map.replicate_attack(event[1], event[2])
        elif event[0] == "JOIN":
            self.nbr_joueurs += 1
            self.bridge.send_event("ACCEPT", self.nbr_joueurs, self.scenario_name)
            time.sleep(0.5)
            for unit in self.units:
                if unit.team == self.team:
                    self.bridge.send_event("UNIT_SPAWN", unit.type, unit.team, unit.id, unit.position[0], unit.position[1])
        # elif event[0] == "UNIT_STATE":

    def update_units(self,time_per_tick):
        for unit in self.units:
            unit.update(time_per_tick)

    def update_projectiles(self):
            self.game_map.update_projectiles()

    def check_victory(self):
        """Vérifie les conditions de victoire"""
        #  Toutes les unités d'un camp détruites

        units_team1 = len([u for u in self.units if u.team == 'R' and u.is_alive])
        units_team2 = len([u for u in self.units if u.team == 'B' and u.is_alive])

        # selection du winner gagne si tout les adverse sont mort
        if units_team1 == 0 and units_team2 == 0:
            self.winner = None
            self.is_running = False
        elif units_team1 == 0:
            self.winner = self.ia2
            self.is_running = False
        elif units_team2 == 0:
            self.winner = self.ia1
            self.is_running = False

        if self.current_turn > self.max_turns:
            self.winner = None
            self.is_running = False
        pass

    def update_view(self):
        """Met à jour l'affichage pour refléter l'état actuel"""
        a = self.view.display(self.game_map, self.get_game_info())

        if a['pause']:
            self.game_pause = not self.game_pause
        if a["quit"]:
            self.end_battle()


    def get_game_info(self):
        """Retourne les informations de jeu à afficher"""

        return {
            'turn': self.current_turn,
            'game_pause': self.game_pause,
            'target_tps' : self.tps,
            'real_tps': mean(self.tab_tps_affichage),
            'turn_fps': round(self.turn_fps),
            'time_from_start': f'{(time.time() - self.star_execution_time):.2f}s',
            'in_game_time': f'{(self.current_turn / 60):.2f}s',
            'performance': f'{round(self.real_tps*100 / 60)}%',
            'time_delta': f'{((self.current_turn / 60)-(time.time() - self.star_execution_time)):.2f}s',
        }
