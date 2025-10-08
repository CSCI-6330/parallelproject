variable "project_name" {
  description = "A short name used to tag/name resources."
  type        = string
  default     = "kafka-hdfs-pipeline"
}

variable "aws_region" {
  description = "AWS region to deploy into."
  type        = string
  default     = "us-east-1"
}

variable "vpc_cidr" {
  description = "VPC CIDR range."
  type        = string
  default     = "10.20.0.0/16"
}

variable "az_count" {
  description = "How many AZs to spread across (2 or 3 recommended)."
  type        = number
  default     = 2
}

variable "public_subnet_cidrs" {
  description = "CIDRs for public subnets (one per AZ)."
  type        = list(string)
  default     = ["10.20.0.0/24", "10.20.1.0/24"]
}

variable "private_subnet_cidrs" {
  description = "CIDRs for private subnets (one per AZ)."
  type        = list(string)
  default     = ["10.20.10.0/24", "10.20.11.0/24"]
}

variable "msk_kafka_version" {
  description = "Kafka version for MSK."
  type        = string
  default     = "3.6.0"
}

variable "msk_broker_instance_type" {
  description = "MSK broker instance type."
  type        = string
  default     = "kafka.m5.large"
}

variable "msk_broker_count" {
  description = "Total number of broker nodes (must be multiple of AZs)."
  type        = number
  default     = 2
}

variable "emr_release" {
  description = "EMR release label."
  type        = string
  default     = "emr-6.15.0"
}

variable "emr_master_instance_type" {
  description = "EMR master instance type."
  type        = string
  default     = "m5.xlarge"
}

variable "emr_core_instance_type" {
  description = "EMR core instance type."
  type        = string
  default     = "m5.xlarge"
}

variable "emr_core_instance_count" {
  description = "Number of EMR core nodes."
  type        = number
  default     = 2
}

variable "ssh_ingress_cidr" {
  description = "CIDR allowed to SSH to bastion/EMR master (lock to your IP!)."
  type        = string
  default     = "0.0.0.0/0"
}

variable "enable_bastion" {
  description = "Create an EC2 bastion in the public subnet for CLI access."
  type        = bool
  default     = true
}

variable "key_pair_name" {
  description = "Existing EC2 key pair name for SSH (required if enable_bastion=true)."
  type        = string
  default     = ""
}
