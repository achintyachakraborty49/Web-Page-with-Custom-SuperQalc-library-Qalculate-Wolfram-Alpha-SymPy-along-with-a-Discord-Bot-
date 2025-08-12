import discord
import subprocess
import asyncio

intents = discord.Intents.default()
intents.message_content = True  # Required to read message content

TOKEN = 'YOUR_DISCORD_BOT_TOKEN'  # Replace this with your token
client = discord.Client(intents=intents)

# Store user expressions temporarily
user_expressions = {}

async def run_command(cmd_path, expression):
    proc = subprocess.run([cmd_path], input=expression.encode(), capture_output=True)
    return proc.stdout.decode().strip()

async def send_long_message(channel, message):
    # Split the message if it exceeds 2000 characters
    while len(message) > 2000:
        await channel.send(message[:2000])
        message = message[2000:]
    if message:
        await channel.send(message)

@client.event
async def on_ready():
    print(f'Logged in as {client.user}!')

@client.event
async def on_message(message):
    if message.author.bot:
        return  # Ignore messages from bots

    content = message.content.strip()

    # Initialize the user's expression list if it doesn't exist
    if message.author.id not in user_expressions:
        user_expressions[message.author.id] = []

    # If the user sends "evaluate", we process the input
    if content.lower() == "evaluate":
        full_expression = "".join(user_expressions[message.author.id]).strip()
        user_expressions[message.author.id] = []  # Reset after evaluation

        if full_expression.lower().startswith("tower "):
            expr = full_expression[6:].strip()
            try:
                output = await run_command('./advikmathlib/superqalc_tower', expr)
                await send_long_message(message.channel, f"**Tower result:**\n```\n{output}\n```")
            except Exception as e:
                await message.channel.send(f"Error running tower command: {e}")
        else:
            try:
                output = await run_command('./advikmathlib/superqalc_onefile', full_expression)
                await send_long_message(message.channel, f"**Result:**\n```\n{output}\n```")
            except Exception as e:
                await message.channel.send(f"Error running onefile command: {e}")
    
    else:
        # Append the new input to the list of expressions
        user_expressions[message.author.id].append(content)

client.run(TOKEN)
