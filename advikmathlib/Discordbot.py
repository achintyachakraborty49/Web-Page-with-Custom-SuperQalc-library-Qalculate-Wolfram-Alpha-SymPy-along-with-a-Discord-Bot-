import discord
from discord.ext import commands
import subprocess
import sympy

bot = commands.Bot(command_prefix="!")

# To store user inputs: {user_id: [list_of_msgs]}
user_inputs = {}

@bot.event
async def on_message(message):
    if message.author.bot:
        return

    user_id = message.author.id
    content = message.content.strip()

    # Initialize list if new user
    if user_id not in user_inputs:
        user_inputs[user_id] = []

    # Add the message to user inputs
    user_inputs[user_id].append(content)

    # If last message is 'evaluate', process the inputs
    if content.lower() == "evaluate":
        # Join all but last message (which is "evaluate"), no spaces
        expr = "".join(user_inputs[user_id][:-1])

        # Clear stored inputs
        user_inputs[user_id] = []

        # Check if input started with 'sympy'
        if expr.lower().startswith("sympy"):
            expr_to_eval = expr[len("sympy"):].strip()
            try:
                result = str(sympy.sympify(expr_to_eval))
            except Exception as e:
                result = f"SymPy Error: {e}"
        else:
            # Use superqalc executables
            # Decide which executable based on your logic; example: tower if starts with 'tower'
            if expr.lower().startswith("tower"):
                to_eval = expr[len("tower"):].strip()
                proc = subprocess.run(["./superqalc_tower"], input=to_eval.encode(), capture_output=True)
            else:
                proc = subprocess.run(["./superqalc_onefile"], input=expr.encode(), capture_output=True)

            if proc.returncode == 0:
                result = proc.stdout.decode()
            else:
                result = "Error running superqalc."

        # Split and send if > 2000 chars
        for chunk_start in range(0, len(result), 2000):
            await message.channel.send(result[chunk_start:chunk_start + 2000])
    else:
        # If not evaluate, do nothing or you can add a reaction to show input received
        pass

    # Process commands if you use any
    await bot.process_commands(message)

bot.run("YOUR_BOT_TOKEN")
