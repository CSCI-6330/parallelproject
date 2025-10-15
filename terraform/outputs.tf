output "s3_bucket" {
  value       = aws_s3_bucket.data.id
  description = "S3 bucket for logs/data."
}

output "msk_bootstrap_brokers" {
  value       = aws_msk_cluster.this.bootstrap_brokers
  description = "PLAINTEXT Kafka bootstrap brokers for clients inside VPC."
}

output "emr_master_public_dns" {
  # EMR master DNS becomes available once running
  value       = aws_emr_cluster.this.master_public_dns
  description = "EMR master public DNS (if internet accessible)."
}

output "bastion_public_ip" {
  value       = try(aws_instance.bastion[0].public_ip, null)
  description = "Bastion IP if created."
}
