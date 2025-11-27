import mysql.connector
from dotenv import load_dotenv
import os

load_dotenv()

def conectar_db():
    try:
       mydb = mysql.connector.connect(
           host=os.getenv("DB_HOST"),
           user=os.getenv("DB_USER"),
           password=os.getenv("DB_PASS"),
           database=os.getenv("DB_NAME")
       )
       return mydb
    except Exception as error:
        return print("Error conectando a la BD:", error)
