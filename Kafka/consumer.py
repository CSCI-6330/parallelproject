from kafka import KafkaConsumer

consumer = KafkaConsumer('gutenberg_topic',
                         bootstrap_servers='<EC2_PUBLIC_IP>:9092',
                         auto_offset_reset='earliest',
                         enable_auto_commit=True)

for message in consumer:
    text = message.value.decode('utf-8')
    print(f"Received {len(text.split())} words")
