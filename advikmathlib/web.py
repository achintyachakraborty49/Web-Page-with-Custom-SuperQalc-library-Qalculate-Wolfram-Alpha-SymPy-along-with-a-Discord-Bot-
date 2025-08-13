from fastapi import FastAPI, HTTPException
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel
import subprocess
import sympy
import requests
import copy
import chess
import random
from typing import Optional

# === CONFIG ===
QALCULATE_PATH = "/data/data/com.termux/files/usr/bin/qalc"
WOLFRAM_APPID = "6JP9U2AAW4"

# === APP INIT ===
app = FastAPI(title="Math & Games API")

# Serve front-end from /static to avoid conflicts
app.mount("/static", StaticFiles(directory=".", html=True), name="static")

# === STORAGE ===
active_chess_games = {}
active_ttt_games = {}

# === MODELS ===
class CalcRequest(BaseModel):
    expression: str

class ChessMoveRequest(BaseModel):
    user1_id: str
    user2_id: str
    move: Optional[str] = None
    action: str  # "start", "move", "resign"

class TTTMoveRequest(BaseModel):
    user_id: str
    position: Optional[int] = None  # 1-9

class SudokuRequest(BaseModel):
    puzzle: str

# === CALCULATORS ===
@app.post("/calc/qalc")
def calc_qalc(req: CalcRequest):
    expr = req.expression.strip()
    try:
        proc = subprocess.run([QALCULATE_PATH, expr], capture_output=True, text=True)
        return {"result": proc.stdout.strip() if proc.returncode == 0 else "Error running Qalc."}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/calc/sympy")
def calc_sympy(req: CalcRequest):
    try:
        return {"result": str(sympy.sympify(req.expression))}
    except Exception as e:
        raise HTTPException(status_code=400, detail=f"SymPy Error: {e}")

@app.post("/calc/wolfram")
def calc_wolfram(req: CalcRequest):
    try:
        url = "http://api.wolframalpha.com/v2/query"
        params = {"input": req.expression, "appid": WOLFRAM_APPID, "output": "JSON"}
        data = requests.get(url, params=params).json()
        pods = data.get("queryresult", {}).get("pods", [])
        for pod in pods:
            if pod["title"].lower() in ["result", "solution"]:
                return {"result": pod["subpods"][0]["plaintext"]}
        return {"result": "No answer found."}
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Wolfram Alpha error: {e}")

# === CHESS ===
@app.post("/chess")
def chess_game(req: ChessMoveRequest):
    key = frozenset({req.user1_id, req.user2_id})
    if req.action == "start":
        if key in active_chess_games:
            raise HTTPException(status_code=400, detail="Game already exists.")
        board = chess.Board()
        active_chess_games[key] = {"board": board, "white": req.user1_id, "black": req.user2_id}
        return {"message": "Chess game started!", "board": board.fen()}

    if key not in active_chess_games:
        raise HTTPException(status_code=404, detail="No active game.")

    game = active_chess_games[key]
    board = game["board"]

    if req.action == "resign":
        del active_chess_games[key]
        return {"message": f"{req.user1_id} resigned."}

    if req.action == "move":
        try:
            move_obj = chess.Move.from_uci(req.move)
            if move_obj not in board.legal_moves:
                raise ValueError("Illegal move")
            board.push(move_obj)
        except Exception as e:
            raise HTTPException(status_code=400, detail=str(e))

        result = {"board": board.fen()}
        if board.is_checkmate():
            result["message"] = f"Checkmate! {req.user1_id} wins!"
            del active_chess_games[key]
        elif board.is_stalemate():
            result["message"] = "Stalemate! Draw."
            del active_chess_games[key]
        else:
            result["message"] = "Move accepted."
        return result

# === TIC TAC TOE ===
EMPTY = "⬜"
PLAYER = "❌"
AI = "⭕"

class TicTacToe:
    def __init__(self):
        self.board = [EMPTY]*9
        self.current_turn = PLAYER
    def display_board(self):
        return "".join(self.board)
    def get_valid_moves(self):
        return [i for i, val in enumerate(self.board) if val == EMPTY]
    def make_move(self, index, symbol):
        self.board[index] = symbol
    def check_winner(self, symbol):
        lines = [[0,1,2],[3,4,5],[6,7,8],[0,3,6],[1,4,7],[2,5,8],[0,4,8],[2,4,6]]
        return any(all(self.board[i]==symbol for i in line) for line in lines)
    def is_full(self):
        return EMPTY not in self.board

def minimax(game, maximizing):
    if game.check_winner(AI): return 1
    if game.check_winner(PLAYER): return -1
    if game.is_full(): return 0
    if maximizing:
        best_score = -float("inf")
        for m in game.get_valid_moves():
            g = copy.deepcopy(game)
            g.make_move(m, AI)
            best_score = max(best_score, minimax(g, False))
        return best_score
    else:
        best_score = float("inf")
        for m in game.get_valid_moves():
            g = copy.deepcopy(game)
            g.make_move(m, PLAYER)
            best_score = min(best_score, minimax(g, True))
        return best_score

def get_best_move(game):
    best_score = -float("inf")
    move = None
    for m in game.get_valid_moves():
        g = copy.deepcopy(game)
        g.make_move(m, AI)
        score = minimax(g, False)
        if score > best_score:
            best_score, move = score, m
    return move

@app.post("/tictactoe")
def ttt_start(req: TTTMoveRequest):
    game = TicTacToe()
    active_ttt_games[req.user_id] = game
    return {"message": "Tic Tac Toe started!", "board": game.display_board()}

@app.post("/tictactoe/move")
def ttt_move(req: TTTMoveRequest):
    if req.user_id not in active_ttt_games:
        raise HTTPException(status_code=404, detail="No active game.")
    game = active_ttt_games[req.user_id]
    index = req.position - 1
    if index not in game.get_valid_moves():
        raise HTTPException(status_code=400, detail="Invalid move")
    game.make_move(index, PLAYER)
    if game.check_winner(PLAYER):
        del active_ttt_games[req.user_id]
        return {"board": game.display_board(), "message": "You win!"}
    if game.is_full():
        del active_ttt_games[req.user_id]
        return {"board": game.display_board(), "message": "Draw!"}
    ai_index = get_best_move(game)
    game.make_move(ai_index, AI)
    if game.check_winner(AI):
        del active_ttt_games[req.user_id]
        return {"board": game.display_board(), "message": "AI wins!"}
    return {"board": game.display_board(), "message": "Your turn!"}

# === SUDOKU ===
@app.post("/sudoku/solve")
def sudoku_solve(req: SudokuRequest):
    try:
        grid = [[int(x) for x in row.split()] for row in req.puzzle.split("\n")]
        def is_valid(grid,r,c,n):
            for i in range(9):
                if grid[r][i]==n or grid[i][c]==n: return False
            br, bc = 3*(r//3),3*(c//3)
            for i in range(br,br+3):
                for j in range(bc,bc+3):
                    if grid[i][j]==n: return False
            return True
        def solve(grid):
            for r in range(9):
                for c in range(9):
                    if grid[r][c]==0:
                        for n in range(1,10):
                            if is_valid(grid,r,c,n):
                                grid[r][c]=n
                                if solve(grid): return True
                                grid[r][c]=0
                        return False
            return True
        if solve(grid):
            return {"solution": grid}
        else:
            raise HTTPException(status_code=400, detail="No solution found")
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))
