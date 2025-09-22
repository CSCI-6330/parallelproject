from kafka import KafkaProducer
import os

producer = KafkaProducer(bootstrap_servers='<EC2_PUBLIC_IP>:9092')
folder = "mock_books/"

for fname in os.listdir(folder):
    path = os.path.join(folder, fname)
    with open(path, 'r') as f:
        text = f.read().encode('utf-8')
        producer.send('gutenberg_topic', value=text)
producer.flush()
print("All files sent to Kafka!")
